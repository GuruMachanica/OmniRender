// filepath: modules/ui/control_panel.cpp
// OmniRender Control Center - Modern, Sleek Obsidian Dark Theme Desktop GUI

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <string>
#include <filesystem>
#include "control_panel_theme.h"
#include "game_detector.h"
#include "../daemon/capability_matrix.h"
#include "../common/config.h"

#pragma comment(lib, "comctl32.lib")

namespace {

omnirender::ui::ThemeResources g_theme;
HWND g_hwnd_main      = nullptr;
HWND g_edit_game_path = nullptr;
HWND g_lbl_detected   = nullptr;
HWND g_combo_upscaler = nullptr;
HWND g_slider_sharp   = nullptr;
HWND g_lbl_sharpness  = nullptr;
HWND g_chk_rt         = nullptr;
HWND g_chk_tonemap    = nullptr;
HWND g_chk_reconstruct= nullptr;
HWND g_combo_res      = nullptr;
HWND g_lbl_status     = nullptr;
HWND g_btn_launch     = nullptr;
HWND g_btn_browse     = nullptr;
HWND g_btn_autodetect = nullptr;

std::wstring g_selected_exe;

void UpdateDetectedApi(const std::wstring& path) {
    try {
        if (path.empty() || !std::filesystem::exists(path)) {
            SetWindowTextW(g_lbl_detected, L"Detected API: Select a game executable");
            return;
        }
        auto det = omnirender::daemon::InspectExecutable(path);
        std::wstring text = L"Target Graphics API: ";
        const char* p = omnirender::daemon::RendererApiName(det.primary);
        text.append(p, p + strlen(p));
        if (!det.detected.empty()) {
            text += L"  [Compatible: ";
            for (size_t i = 0; i < det.detected.size(); ++i) {
                const char* n = omnirender::daemon::RendererApiName(det.detected[i]);
                text.append(n, n + strlen(n));
                if (i + 1 < det.detected.size()) text += L", ";
            }
            text += L"]";
        }
        SetWindowTextW(g_lbl_detected, text.c_str());
    } catch (...) {
        SetWindowTextW(g_lbl_detected, L"Target Graphics API: Unknown");
    }
}

void OnBrowse() {
    wchar_t filename[MAX_PATH] = {};
    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.hwndOwner = g_hwnd_main;
    ofn.lpstrFilter = L"Game Executable (*.exe)\0*.exe\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) {
        g_selected_exe = filename;
        SetWindowTextW(g_edit_game_path, filename);
        UpdateDetectedApi(filename);
    }
}

void OnAutoDetect() {
    try {
        SetWindowTextW(g_lbl_status, L"Scanning running games...");
        std::wstring detected = omnirender::ui::AutoDetectTargetExecutable();
        if (!detected.empty()) {
            g_selected_exe = detected;
            SetWindowTextW(g_edit_game_path, g_selected_exe.c_str());
            UpdateDetectedApi(g_selected_exe);
            std::wstring name = std::filesystem::path(g_selected_exe).filename().wstring();
            SetWindowTextW(g_lbl_status, (L"Active: " + name).c_str());
            return;
        }
        SetWindowTextW(g_lbl_status, L"Ready to Launch");
    } catch (...) {
        SetWindowTextW(g_lbl_status, L"Ready to Launch");
    }
}

void SaveCurrentConfig(const omnirender::ui::LaunchOptions& opt) {
    try {
        auto global_path = omnirender::config::GlobalConfigPath();
        if (!global_path.empty()) {
            omnirender::config::ConfigStore cfg;
            cfg.LoadFromFile(global_path);
            cfg.Set("pipeline.enable_reconstruction", opt.enable_reconstruction ? "true" : "false");
            cfg.Set("pipeline.enable_upscale", "true");
            cfg.Set("pipeline.enable_tonemap", opt.enable_tonemap ? "true" : "false");
            cfg.Set("pipeline.enable_rt_effects", opt.enable_rt ? "true" : "false");
            cfg.Set("pipeline.enable_fsr", (opt.upscaler_index == 1 || opt.upscaler_index == 0) ? "true" : "false");
            cfg.Set("pipeline.enable_dlss", (opt.upscaler_index == 2) ? "true" : "false");
            cfg.Set("pipeline.enable_xess", (opt.upscaler_index == 3) ? "true" : "false");
            cfg.Set("pipeline.output_resolution_index", std::to_string(opt.output_resolution_index));
            cfg.Set("pipeline.sharpness", std::to_string(opt.sharpness));
            cfg.SaveToFile(global_path);
        }
    } catch (...) {}
}

void OnLaunchGame() {
    wchar_t buf[MAX_PATH] = {};
    GetWindowTextW(g_edit_game_path, buf, MAX_PATH);
    omnirender::ui::LaunchOptions opt{};
    opt.game_path = buf;
    opt.upscaler_index = (int)SendMessageW(g_combo_upscaler, CB_GETCURSEL, 0, 0);
    opt.output_resolution_index = (int)SendMessageW(g_combo_res, CB_GETCURSEL, 0, 0);
    opt.sharpness = static_cast<float>(SendMessageW(g_slider_sharp, TBM_GETPOS, 0, 0)) / 100.0f;
    opt.enable_rt = (SendMessageW(g_chk_rt, BM_GETCHECK, 0, 0) == BST_CHECKED);
    opt.enable_tonemap = (SendMessageW(g_chk_tonemap, BM_GETCHECK, 0, 0) == BST_CHECKED);
    opt.enable_reconstruction = (SendMessageW(g_chk_reconstruct, BM_GETCHECK, 0, 0) == BST_CHECKED);
    SaveCurrentConfig(opt);

    std::wstring err;
    if (omnirender::ui::LaunchGameAndDaemon(opt, err)) {
        SetWindowTextW(g_lbl_status, L"OmniRender Pipeline Active");
    } else {
        MessageBoxW(g_hwnd_main, err.c_str(), L"OmniRender", MB_ICONWARNING);
    }
}

void SetControlFont(HWND hwnd, HFONT font) {
    SendMessageW(hwnd, WM_SETFONT, (WPARAM)font, TRUE);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            HWND hTitle = CreateWindowW(L"STATIC", L"OmniRender Control Center", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 28, 20, 420, 28, hwnd, nullptr, nullptr, nullptr);
            SetControlFont(hTitle, g_theme.font_title);
            HWND hSub = CreateWindowW(L"STATIC", L"AI Reconstruction, Real-Time Ray Tracing & Spatial Upscaling", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 28, 50, 520, 20, hwnd, nullptr, nullptr, nullptr);
            SetControlFont(hSub, g_theme.font_subtitle);

            HWND hLblGame = CreateWindowW(L"STATIC", L"Target Game Executable", WS_CHILD | WS_VISIBLE | SS_NOPREFIX, 28, 86, 200, 18, hwnd, nullptr, nullptr, nullptr);
            SetControlFont(hLblGame, g_theme.font_bold);
            g_edit_game_path = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 28, 108, 335, 28, hwnd, nullptr, nullptr, nullptr);
            SetControlFont(g_edit_game_path, g_theme.font_body);
            g_btn_browse = CreateWindowW(L"BUTTON", L"Browse...", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 372, 108, 88, 28, hwnd, (HMENU)101, nullptr, nullptr);
            g_btn_autodetect = CreateWindowW(L"BUTTON", L"Auto-Detect", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 468, 108, 100, 28, hwnd, (HMENU)103, nullptr, nullptr);
            g_lbl_detected = CreateWindowW(L"STATIC", L"Target Graphics API: Auto-detect on executable selection", WS_CHILD | WS_VISIBLE | SS_NOPREFIX, 28, 142, 540, 18, hwnd, nullptr, nullptr, nullptr);
            SetControlFont(g_lbl_detected, g_theme.font_small);

            HWND hLblUpscale = CreateWindowW(L"STATIC", L"Upscaling Engine", WS_CHILD | WS_VISIBLE | SS_NOPREFIX, 28, 178, 160, 18, hwnd, nullptr, nullptr, nullptr);
            SetControlFont(hLblUpscale, g_theme.font_bold);
            g_combo_upscaler = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 28, 202, 260, 150, hwnd, nullptr, nullptr, nullptr);
            SendMessageW(g_combo_upscaler, CB_ADDSTRING, 0, (LPARAM)L"Auto (Best Hardware Match)");
            SendMessageW(g_combo_upscaler, CB_ADDSTRING, 0, (LPARAM)L"AMD FSR 1.0 (EASU + RCAS)");
            SendMessageW(g_combo_upscaler, CB_ADDSTRING, 0, (LPARAM)L"NVIDIA DLSS (NGX)");
            SendMessageW(g_combo_upscaler, CB_ADDSTRING, 0, (LPARAM)L"Intel XeSS");
            SendMessageW(g_combo_upscaler, CB_ADDSTRING, 0, (LPARAM)L"Native Bounded Bicubic");
            SendMessageW(g_combo_upscaler, CB_SETCURSEL, 0, 0);
            SetControlFont(g_combo_upscaler, g_theme.font_body);

            HWND hLblSharp = CreateWindowW(L"STATIC", L"Sharpening Intensity", WS_CHILD | WS_VISIBLE | SS_NOPREFIX, 316, 178, 160, 18, hwnd, nullptr, nullptr, nullptr);
            SetControlFont(hLblSharp, g_theme.font_bold);
            g_slider_sharp = CreateWindowW(TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS, 310, 202, 200, 28, hwnd, nullptr, nullptr, nullptr);
            SendMessageW(g_slider_sharp, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
            SendMessageW(g_slider_sharp, TBM_SETPOS, TRUE, 75);
            g_lbl_sharpness = CreateWindowW(L"STATIC", L"75%", WS_CHILD | WS_VISIBLE | SS_NOPREFIX, 520, 206, 45, 20, hwnd, nullptr, nullptr, nullptr);
            SetControlFont(g_lbl_sharpness, g_theme.font_bold);

            HWND hLblEnhance = CreateWindowW(L"STATIC", L"Neural Visual Enhancements", WS_CHILD | WS_VISIBLE | SS_NOPREFIX, 28, 248, 240, 18, hwnd, nullptr, nullptr, nullptr);
            SetControlFont(hLblEnhance, g_theme.font_bold);
            g_chk_rt = CreateWindowW(L"BUTTON", L"Screen-Space Ray Tracing (SSR Reflections & Contact AO)", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 28, 274, 530, 24, hwnd, nullptr, nullptr, nullptr);
            SendMessageW(g_chk_rt, BM_SETCHECK, BST_CHECKED, 0);
            SetControlFont(g_chk_rt, g_theme.font_body);
            g_chk_tonemap = CreateWindowW(L"BUTTON", L"Auto HDR Inverse Tonemapper (Reinhard Extended)", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 28, 304, 530, 24, hwnd, nullptr, nullptr, nullptr);
            SendMessageW(g_chk_tonemap, BM_SETCHECK, BST_CHECKED, 0);
            SetControlFont(g_chk_tonemap, g_theme.font_body);
            g_chk_reconstruct = CreateWindowW(L"BUTTON", L"Multi-Frame Temporal History Reconstruction", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 28, 334, 530, 24, hwnd, nullptr, nullptr, nullptr);
            SendMessageW(g_chk_reconstruct, BM_SETCHECK, BST_CHECKED, 0);
            SetControlFont(g_chk_reconstruct, g_theme.font_body);

            HWND hLblRes = CreateWindowW(L"STATIC", L"Target Output Resolution", WS_CHILD | WS_VISIBLE | SS_NOPREFIX, 28, 376, 200, 18, hwnd, nullptr, nullptr, nullptr);
            SetControlFont(hLblRes, g_theme.font_bold);
            g_combo_res = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 28, 400, 260, 150, hwnd, nullptr, nullptr, nullptr);
            SendMessageW(g_combo_res, CB_ADDSTRING, 0, (LPARAM)L"Auto (Display Native)");
            SendMessageW(g_combo_res, CB_ADDSTRING, 0, (LPARAM)L"1920 x 1080 (1080p FHD)");
            SendMessageW(g_combo_res, CB_ADDSTRING, 0, (LPARAM)L"2560 x 1440 (1440p QHD)");
            SendMessageW(g_combo_res, CB_ADDSTRING, 0, (LPARAM)L"3840 x 2160 (4K UHD)");
            SendMessageW(g_combo_res, CB_SETCURSEL, 0, 0);
            SetControlFont(g_combo_res, g_theme.font_body);

            g_btn_launch = CreateWindowW(L"BUTTON", L"Launch Game with OmniRender", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 28, 456, 310, 44, hwnd, (HMENU)102, nullptr, nullptr);
            g_lbl_status = CreateWindowW(L"STATIC", L"Status: Ready to Launch", WS_CHILD | WS_VISIBLE | SS_NOPREFIX, 350, 468, 220, 24, hwnd, nullptr, nullptr, nullptr);
            SetControlFont(g_lbl_status, g_theme.font_bold);
            return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == 101) OnBrowse();
            else if (id == 102) OnLaunchGame();
            else if (id == 103) OnAutoDetect();
            return 0;
        }
        case WM_DRAWITEM: {
            auto* pDIS = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (!pDIS) break;
            if (pDIS->CtlID == 102) {
                omnirender::ui::DrawLaunchButton(*pDIS, g_theme.font_bold);
                return TRUE;
            } else if (pDIS->CtlID == 101 || pDIS->CtlID == 103) {
                const wchar_t* text = (pDIS->CtlID == 101) ? L"Browse..." : L"Auto-Detect";
                omnirender::ui::DrawSecondaryButton(*pDIS, text, g_theme.font_body);
                return TRUE;
            }
            break;
        }
        case WM_HSCROLL: {
            if ((HWND)lParam == g_slider_sharp) {
                LRESULT pos = SendMessageW(g_slider_sharp, TBM_GETPOS, 0, 0);
                SetWindowTextW(g_lbl_sharpness, (std::to_wstring(pos) + L"%").c_str());
            }
            return 0;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
            return omnirender::ui::HandleCtlColorStatic((HDC)wParam, g_theme);
        case WM_CTLCOLOREDIT:
            return omnirender::ui::HandleCtlColorEdit((HDC)wParam, g_theme);
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);
    omnirender::ui::InitTheme(g_theme);

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = g_theme.bg_brush;
    wc.lpszClassName = L"OmniRenderControlCenter";
    RegisterClassExW(&wc);

    int sx = GetSystemMetrics(SM_CXSCREEN);
    int sy = GetSystemMetrics(SM_CYSCREEN);
    int win_w = 605, win_h = 560;

    g_hwnd_main = CreateWindowExW(
        WS_EX_APPWINDOW, wc.lpszClassName, L"OmniRender Control Center",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE,
        (sx - win_w) / 2, (sy - win_h) / 2, win_w, win_h,
        nullptr, nullptr, hInstance, nullptr);

    if (!g_hwnd_main) return 1;

    ShowWindow(g_hwnd_main, SW_SHOWNORMAL);
    UpdateWindow(g_hwnd_main);
    SetForegroundWindow(g_hwnd_main);

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    omnirender::ui::CleanupTheme(g_theme);
    return (int)msg.wParam;
}
