// filepath: tools/launcher/OmniRenderLaunch.cpp
// OmniRender game launcher — the real-game testing entry point.
//
// What it does, in order:
//   1. Creates the target game process SUSPENDED.
//   2. Drops the OmniRender hook proxy DLL (d3d9.dll / dxgi.dll /
//      opengl32.dll) next to the game exe, backing up any original the
//      game shipped with to <name>.omnirender.bak (idempotent: a second
//      run refreshes the proxy and keeps the first backup).
//   3. Writes config.toml next to the launcher (never overwrites an
//      existing one) so users can tune output scale / backend before or
//      between runs.
//   4. Starts OmniRenderDaemon.exe with the game exe path as its first
//      argument (the daemon hashes the path for per-game config).
//   5. Resumes the game. The proxy DLL loads through the normal loader,
//      initializes IPC, and the daemon overlay appears when the first
//      frame is captured.
//
// Exit codes: 0 = launched, 2 = usage error, 3 = file/setup error.
//
// Usage:
//   OmniRenderLaunch.exe <game.exe> [game arguments...]

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kDaemonName[] = L"OmniRenderDaemon.exe";

// The hook build emits all three proxy names (see modules/hook/CMakeLists.txt
// POST_BUILD copies). The launcher drops the ones that exist in its own
// directory — the DLLMain proxy-role detection inside the hook keeps the
// behavior correct whichever ones land. Proxy roles:
//   d3d9.dll     exports Direct3DCreate9
//   dxgi.dll     exports CreateDXGIFactory{,1,2}
//   opengl32.dll forwards wglSwapBuffers
constexpr const wchar_t* kProxies[] = { L"d3d9.dll", L"dxgi.dll", L"opengl32.dll" };

std::wstring ExeDir() {
    wchar_t path[MAX_PATH]{};
    ::GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring dir(path);
    const size_t slash = dir.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? L"." : dir.substr(0, slash);
}

std::wstring JoinPath(const std::wstring& a, const std::wstring& b) {
    if (!a.empty() && a.back() != L'\\' && a.back() != L'/') return a + L"\\" + b;
    return a + b;
}

bool FileExists(const std::wstring& p) {
    return ::GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}

void Fail(const wchar_t* msg, const std::wstring& detail = L"") {
    std::fwprintf(stderr, L"[OmniRender] ERROR: %s%s\n", msg,
                  detail.empty() ? L"" : (L" — " + detail).c_str());
}

int WriteDefaultConfig(const std::wstring& config_path) {
    if (FileExists(config_path)) return 0;  // never clobber user tuning
    static constexpr char kConfig[] =
        "# OmniRender per-launch config (written once by the launcher).\n"
        "[renderer]\n"
        "upscaler = \"auto\"        # auto | dlss | xess | fsr | off\n"
        "output_scale = \"screen\"  # native | screen | quality | ultra | custom\n"
        "sharpen = 0.75\n"
        "\n"
        "[pipeline]\n"
        "enable_upscale = true\n"
        "enable_depth_linearize = true\n"
        "enable_motion_vectors = true\n";
    HANDLE f = ::CreateFileW(config_path.c_str(), GENERIC_WRITE, 0, nullptr,
                             CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return (::GetLastError() == ERROR_FILE_EXISTS) ? 0 : -1;
    DWORD written = 0;
    ::WriteFile(f, kConfig, static_cast<DWORD>(sizeof(kConfig) - 1), &written, nullptr);
    ::CloseHandle(f);
    return 0;
}

// Read the PE machine type of the game executable so the launcher picks the
// proxy DLLs matching the game's bitness. Returns 0x014c (I386, 32-bit),
// 0x8664 (AMD64), or 0 when unknown.
uint16_t ReadPEMachine(const std::wstring& exe) {
    std::FILE* f = nullptr;
    if (_wfopen_s(&f, exe.c_str(), L"rb") != 0 || !f) return 0;
    uint8_t dos[2] = {};
    if (std::fread(dos, 1, 2, f) != 2 || dos[0] != 'M' || dos[1] != 'Z') {
        std::fclose(f); return 0;
    }
    if (std::fseek(f, 0x3C, SEEK_SET) != 0) { std::fclose(f); return 0; }
    uint32_t pe_off = 0;
    if (std::fread(&pe_off, 4, 1, f) != 1) { std::fclose(f); return 0; }
    if (std::fseek(f, static_cast<long>(pe_off), SEEK_SET) != 0) { std::fclose(f); return 0; }
    uint8_t pe_sig[4] = {};
    if (std::fread(pe_sig, 1, 4, f) != 4 || pe_sig[0] != 'P' || pe_sig[1] != 'E') {
        std::fclose(f); return 0;
    }
    uint16_t machine = 0;
    if (std::fread(&machine, 2, 1, f) != 1) machine = 0;
    std::fclose(f);
    return machine;  // 0x014c = I386 (32-bit), 0x8664 = AMD64 (64-bit)
}

// Directories searched for proxy DLLs, most specific first: the launcher's
// own directory, then the arch-matched hooks\ subfolder of the release
// layout, then both arch folders as a fallback.
std::vector<std::wstring> ProxySearchDirs(const std::wstring& exe_dir, uint16_t machine) {
    std::vector<std::wstring> dirs{ exe_dir };
    const wchar_t* primary = (machine == 0x014c) ? L"hooks\\x86"
                           : (machine == 0x8664) ? L"hooks\\x64" : nullptr;
    if (primary) dirs.push_back(JoinPath(exe_dir, primary));
    dirs.push_back(JoinPath(exe_dir, L"hooks\\x86"));
    dirs.push_back(JoinPath(exe_dir, L"hooks\\x64"));
    return dirs;
}

// Drop one proxy next to the game exe, backing up an existing original.
// Returns 0 on success, -1 on failure, 1 when the proxy source is absent
// (that API simply isn't part of this build — not an error).
int DeployProxy(const std::wstring& src, const std::wstring& dst) {
    if (!FileExists(src)) return 1;

    const std::wstring backup = dst + L".omnirender.bak";
    if (!FileExists(backup) && FileExists(dst)) {
        if (!::CopyFileW(dst.c_str(), backup.c_str(), TRUE)) {
            Fail(L"cannot back up existing DLL", dst);
            return -1;
        }
        std::fwprintf(stdout, L"[OmniRender] backed up original -> %s\n", backup.c_str());
    }

    if (!::CopyFileW(src.c_str(), dst.c_str(), FALSE)) {
        Fail(L"cannot drop proxy DLL (is the game running?)", dst);
        return -1;
    }
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2 || !argv[1] || !argv[1][0]) {
        std::fwprintf(stderr, L"Usage: OmniRenderLaunch.exe <game.exe> [game args...]\n");
        return 2;
    }

    std::wstring game = argv[1];
    if (game.find(L'\\') == std::wstring::npos && game.find(L'/') == std::wstring::npos) {
        Fail(L"game path must include a directory (use an absolute or relative path)", game);
        return 2;
    }
    // Make the game path absolute now: after CreateProcessW we no longer
    // control what the game sees as relative-path context, and the daemon
    // hashes the exe path for per-game config.
    wchar_t game_abs[MAX_PATH]{};
    if (!::GetFullPathNameW(game.c_str(), MAX_PATH, game_abs, nullptr)) {
        Fail(L"cannot resolve game path", game);
        return 2;
    }
    game = game_abs;
    if (!FileExists(game)) {
        Fail(L"game executable not found", game);
        return 3;
    }
    const std::wstring game_dir  = game.substr(0, game.find_last_of(L"\\/"));
    const std::wstring exe_dir   = ExeDir();
    const std::wstring daemon    = JoinPath(exe_dir, kDaemonName);
    const std::wstring config    = JoinPath(exe_dir, L"config.toml");

    if (!FileExists(daemon)) {
        Fail(L"OmniRenderDaemon.exe not found next to the launcher", daemon);
        return 3;
    }

    // Match the proxy DLL bitness to the game: a 32-bit process cannot load
    // a 64-bit d3d9.dll and vice versa. Legacy titles (PoP: The Two Thrones,
    // Jade engine, 2005) are I386; release layouts put them in hooks\\x86.
    const uint16_t machine = ReadPEMachine(game);
    const wchar_t* arch = (machine == 0x014c) ? L"x86 (32-bit)"
                        : (machine == 0x8664) ? L"x64 (64-bit)" : L"unknown";
    std::fwprintf(stdout, L"[OmniRender] game architecture: %s\n", arch);

    // ---- 1. Config ---------------------------------------------------------
    WriteDefaultConfig(config);

    // ---- 2. Proxy deployment ----------------------------------------------
    const std::vector<std::wstring> search_dirs = ProxySearchDirs(exe_dir, machine);
    for (const wchar_t* proxy : kProxies) {
        const std::wstring dst = JoinPath(game_dir, proxy);
        int rc = 1;
        for (const std::wstring& dir : search_dirs) {
            rc = DeployProxy(JoinPath(dir, proxy), dst);
            if (rc <= 0) break;  // deployed, or a hard failure
        }
        if (rc < 0) return 3;
        if (rc == 0) {
            std::fwprintf(stdout, L"[OmniRender] deployed %s -> %s\n",
                          proxy, dst.c_str());
        }
    }

    // ---- 3. Game process (suspended) ---------------------------------------
    // The command line is the game path followed by any user arguments,
    // quoted as one unit for the game path itself.
    std::wstring cmdline = L"\"" + game + L"\"";
    for (int i = 2; i < argc; ++i) {
        cmdline += L" ";
        cmdline += argv[i] ? argv[i] : L"";
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!::CreateProcessW(game.c_str(), cmdline.data(), nullptr, nullptr, FALSE,
                          CREATE_SUSPENDED, nullptr, game_dir.c_str(), &si, &pi)) {
        Fail(L"CreateProcessW failed for the game", game);
        return 3;
    }

    // ---- 4. Daemon ----------------------------------------------------------
    std::wstring daemon_cmd = L"\"" + daemon + L"\" \"" + game + L"\"";
    STARTUPINFOW si_d{};
    si_d.cb = sizeof(si_d);
    PROCESS_INFORMATION pi_d{};
    if (!::CreateProcessW(nullptr, daemon_cmd.data(), nullptr, nullptr, FALSE,
                          0, nullptr, exe_dir.c_str(), &si_d, &pi_d)) {
        // The proxy already loaded (or will load) into the suspended game;
        // fail loudly rather than leave a hook without a daemon.
        ::TerminateProcess(pi.hProcess, 1);
        ::CloseHandle(pi.hThread);
        ::CloseHandle(pi.hProcess);
        Fail(L"cannot start OmniRenderDaemon.exe", daemon);
        return 3;
    }
    ::CloseHandle(pi_d.hThread);
    ::CloseHandle(pi_d.hProcess);

    // Give the daemon a moment to create the IPC ring so the hook's first
    // frame finds it (not fatal — the hook retries opening the block).
    ::Sleep(500);

    // ---- 5. Resume the game -------------------------------------------------
    if (::ResumeThread(pi.hThread) == static_cast<DWORD>(-1)) {
        ::CloseHandle(pi.hThread);
        ::CloseHandle(pi.hProcess);
        Fail(L"ResumeThread failed");
        return 3;
    }

    // Early-exit watchdog: a proxy DLL the game's loader cannot satisfy
    // (missing exports) kills the process before main() with no dialog.
    // Surface that instead of pretending the launch succeeded.
    bool exited_early = false;
    DWORD exit_code = 0;
    for (int i = 0; i < 30; ++i) {  // ~3 s of monitoring
        const DWORD wait = ::WaitForSingleObject(pi.hProcess, 100);
        if (wait == WAIT_OBJECT_0) {
            ::GetExitCodeProcess(pi.hProcess, &exit_code);
            exited_early = true;
            break;
        }
        if (wait == WAIT_FAILED) break;
    }
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);

    if (exited_early) {
        Fail(L"game process exited immediately — the Windows loader could not start it");
        std::fwprintf(stderr,
            L"  exit code: %lu (0x%08lX)\n"
            L"  0xC0000139 = STATUS_ENTRYPOINT_NOT_FOUND: the game imports a function"
            L" our proxy DLL does not export — report the game on the issue tracker.\n"
            L"  0xC0000135 = STATUS_DLL_NOT_FOUND: a different dependency is missing.\n"
            L"  Other codes: try launching the game normally through Steam; if that"
            L" works, the proxies themselves are fine and this is a launch-order issue.\n",
            exit_code, exit_code);
        return 3;
    }

    std::fwprintf(stdout,
        L"[OmniRender] game + daemon running.\n"
        L"  Overlay : F11 toggles HUD, F12 cycles debug views (depth/motion/masks).\n"
        L"  Config  : %s\n"
        L"  Stop    : quit the game, then quit the daemon overlay window.\n",
        config.c_str());
    return 0;
}
