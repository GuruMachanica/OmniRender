// filepath: modules/ui/game_detector.h
#pragma once

#include <string>
#include <vector>

namespace omnirender::ui {

// Scans running processes for 3D DirectX/OpenGL games using PE import analysis.
std::vector<std::wstring> ScanRunning3DGames();

// Scans Steam common installation directories for 3D game executables.
std::vector<std::wstring> ScanInstalledSteamGames();

// Returns candidate 3D game executables (running, installed, or local test host).
std::wstring AutoDetectTargetExecutable();

struct LaunchOptions {
    std::wstring game_path;
    int          upscaler_index = 0;
    int          output_resolution_index = 0;
    float        sharpness = 0.75f;
    bool         enable_rt = true;
    bool         enable_tonemap = true;
    bool         enable_reconstruction = true;
};

// Configures environment and launches daemon and game executable.
bool LaunchGameAndDaemon(const LaunchOptions& opt, std::wstring& err_msg);

}  // namespace omnirender::ui
