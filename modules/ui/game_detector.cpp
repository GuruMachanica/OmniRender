// filepath: modules/ui/game_detector.cpp
// Automatic 3D game and process detection and launcher.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "game_detector.h"
#include "../daemon/capability_matrix.h"

namespace omnirender::ui {

namespace {

bool IsSystemProcess(const std::wstring& name_lower) {
    static const wchar_t* kIgnored[] = {
        L"explorer.exe", L"dwm.exe", L"svchost.exe", L"csrss.exe", L"smss.exe",
        L"lsass.exe", L"winlogon.exe", L"services.exe", L"taskmgr.exe",
        L"devenv.exe", L"cmd.exe", L"powershell.exe", L"pwsh.exe", L"conhost.exe",
        L"omnirenderdaemon.exe", L"omnirendercontrolpanel.exe", L"code.exe",
        L"chrome.exe", L"msedge.exe", L"firefox.exe", L"discord.exe", L"steam.exe",
        L"epicgameslauncher.exe", L"runtimebroker.exe", L"searchhost.exe",
        L"startmenuexperiencehost.exe", L"textinputhost.exe", L"shellexperiencehost.exe"
    };
    for (const auto* ign : kIgnored) {
        if (name_lower == ign) return true;
    }
    return false;
}

}  // namespace

std::vector<std::wstring> ScanRunning3DGames() {
    std::vector<std::wstring> results;
    try {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return results;

        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(snap, &pe)) {
            do {
                std::wstring exe_name = pe.szExeFile;
                std::wstring exe_lower = exe_name;
                std::transform(exe_lower.begin(), exe_lower.end(), exe_lower.begin(), ::towlower);
                if (IsSystemProcess(exe_lower)) continue;

                HANDLE hproc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
                if (hproc) {
                    wchar_t full_path[MAX_PATH] = {};
                    DWORD size = MAX_PATH;
                    if (QueryFullProcessImageNameW(hproc, 0, full_path, &size)) {
                        std::wstring p = full_path;
                        try {
                            auto det = omnirender::daemon::InspectExecutable(p);
                            if (det.primary != omnirender::daemon::RendererApi::Unknown) {
                                results.push_back(p);
                            }
                        } catch (...) {}
                    }
                    CloseHandle(hproc);
                }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
    } catch (...) {}
    return results;
}

std::vector<std::wstring> ScanInstalledSteamGames() {
    std::vector<std::wstring> results;
    try {
        HKEY hkey = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0, KEY_READ, &hkey) == ERROR_SUCCESS) {
            wchar_t steam_path[MAX_PATH] = {};
            DWORD len = sizeof(steam_path);
            if (RegQueryValueExW(hkey, L"SteamPath", nullptr, nullptr, (LPBYTE)steam_path, &len) == ERROR_SUCCESS) {
                std::filesystem::path common = std::filesystem::path(steam_path) / "steamapps" / "common";
                std::error_code ec;
                if (std::filesystem::exists(common, ec)) {
                    for (const auto& game_dir : std::filesystem::directory_iterator(common, std::filesystem::directory_options::skip_permission_denied, ec)) {
                        if (ec) break;
                        if (game_dir.is_directory(ec)) {
                            for (const auto& f : std::filesystem::directory_iterator(game_dir.path(), std::filesystem::directory_options::skip_permission_denied, ec)) {
                                if (ec) break;
                                if (f.path().extension() == ".exe") {
                                    try {
                                        auto det = omnirender::daemon::InspectExecutable(f.path().wstring());
                                        if (det.primary != omnirender::daemon::RendererApi::Unknown) {
                                            results.push_back(f.path().wstring());
                                            if (results.size() >= 5) break;
                                        }
                                    } catch (...) {}
                                }
                            }
                        }
                        if (results.size() >= 5) break;
                    }
                }
            }
            RegCloseKey(hkey);
        }
    } catch (...) {}
    return results;
}

std::wstring AutoDetectTargetExecutable() {
    auto running = ScanRunning3DGames();
    if (!running.empty()) {
        return running.front();
    }
    auto installed = ScanInstalledSteamGames();
    if (!installed.empty()) {
        return installed.front();
    }
    wchar_t self[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::filesystem::path test_host = std::filesystem::path(self).parent_path() / "OmniRenderTestHost.exe";
    if (std::filesystem::exists(test_host)) {
        return test_host.wstring();
    }
    return L"";
}

bool LaunchGameAndDaemon(const LaunchOptions& opt, std::wstring& err_msg) {
    if (opt.game_path.empty() || !std::filesystem::exists(opt.game_path)) {
        err_msg = L"Please select a valid game executable first.";
        return false;
    }

    bool enable_fsr  = (opt.upscaler_index == 1 || opt.upscaler_index == 0);
    bool enable_dlss = (opt.upscaler_index == 2);
    bool enable_xess = (opt.upscaler_index == 3);

    SetEnvironmentVariableW(L"OMNIRENDER_PIPELINE_ENABLE_FSR", enable_fsr ? L"1" : L"0");
    SetEnvironmentVariableW(L"OMNIRENDER_PIPELINE_ENABLE_DLSS", enable_dlss ? L"1" : L"0");
    SetEnvironmentVariableW(L"OMNIRENDER_PIPELINE_ENABLE_XESS", enable_xess ? L"1" : L"0");
    SetEnvironmentVariableW(L"OMNIRENDER_PIPELINE_ENABLE_RT_EFFECTS", opt.enable_rt ? L"1" : L"0");
    SetEnvironmentVariableW(L"OMNIRENDER_ENABLE_TONEMAP", opt.enable_tonemap ? L"1" : L"0");
    SetEnvironmentVariableW(L"OMNIRENDER_ENABLE_RECONSTRUCTION", opt.enable_reconstruction ? L"1" : L"0");

    wchar_t exe_dir[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe_dir, MAX_PATH);
    std::filesystem::path dir = std::filesystem::path(exe_dir).parent_path();
    std::filesystem::path daemon_exe = dir / L"OmniRenderDaemon.exe";

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    if (std::filesystem::exists(daemon_exe)) {
        std::wstring cmd = L"\"" + daemon_exe.wstring() + L"\" \"" + opt.game_path + L"\"";
        CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    std::filesystem::path game_dir = std::filesystem::path(opt.game_path).parent_path();
    std::filesystem::path hook_src = dir / L"omnirender-hook.dll";
    if (std::filesystem::exists(hook_src)) {
        auto det = omnirender::daemon::InspectExecutable(opt.game_path);
        std::wstring proxy_name = L"d3d9.dll";
        if (det.primary == omnirender::daemon::RendererApi::OpenGL) {
            proxy_name = L"opengl32.dll";
        } else if (det.primary == omnirender::daemon::RendererApi::D3D11 ||
                   det.primary == omnirender::daemon::RendererApi::D3D10 ||
                   det.primary == omnirender::daemon::RendererApi::D3D12) {
            proxy_name = L"dxgi.dll";
        }
        std::error_code ec;
        std::filesystem::copy_file(hook_src, game_dir / proxy_name,
                                   std::filesystem::copy_options::overwrite_existing, ec);
    }

    std::wstring game_cmd = L"\"" + opt.game_path + L"\"";
    STARTUPINFOW gsi = { sizeof(gsi) };
    PROCESS_INFORMATION gpi = {};
    if (CreateProcessW(nullptr, game_cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, game_dir.c_str(), &gsi, &gpi)) {
        CloseHandle(gpi.hProcess);
        CloseHandle(gpi.hThread);
        return true;
    }

    err_msg = L"Failed to start the game executable.";
    return false;
}

}  // namespace omnirender::ui
