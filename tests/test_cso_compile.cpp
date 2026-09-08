// filepath: tests/test_cso_compile.cpp
// Verifies the bounded-pipeline CSO files exist in the build tree and
// are non-empty. This is a build-time smoke test; it does not require
// a GPU. A real GPU smoke test that loads a CSO via
// ID3D11Device::CreateComputeShader runs in
// OmniRenderTestHost (see modules/daemon/test_host.cpp).
//
// The test is skipped (not failed) when no CSO directory is present,
// so a stock Visual Studio build without DXC installed still passes.

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <windows.h>

namespace fs = std::filesystem;

static const std::vector<std::string> kExpectedCsos = {
    "passthrough_ps.cso",
    "passthrough_vs.cso",
    "upscale_hlsl.cso",
    "reconstruct_hlsl.cso",
    "depth_linearize_hlsl.cso",
    "auto_hdr_tonemap.cso",
    "fsr_easu.cso",
    "fsr_rcas.cso",
    "raytrace_screen_space.cso",
    "reactive_mask_hlsl.cso",
    "disocclusion_hlsl.cso",
    "motion_reproject_hlsl.cso",
};

int main() {
    wchar_t self_path[MAX_PATH]{};
    fs::path exe_dir;
    if (::GetModuleFileNameW(nullptr, self_path, MAX_PATH) != 0) {
        exe_dir = fs::path(self_path).parent_path();
    }

    std::vector<fs::path> candidates = {
        exe_dir / "shaders",
        exe_dir,
        exe_dir / ".." / "modules" / "daemon" / "shaders",
        exe_dir / ".." / "modules" / "daemon" / "Release",
        exe_dir / ".." / "modules" / "daemon" / "Debug",
        exe_dir.parent_path() / "modules" / "daemon" / "shaders",
        exe_dir.parent_path() / "modules" / "daemon" / "Release",
        exe_dir.parent_path() / "modules" / "daemon" / "Debug",
        fs::current_path() / "shaders",
        fs::current_path(),
        fs::current_path() / "modules" / "daemon" / "shaders",
        fs::current_path() / "modules" / "daemon" / "Release",
        fs::current_path() / "modules" / "daemon" / "Debug",
        fs::current_path().parent_path() / "modules" / "daemon" / "shaders",
        fs::current_path().parent_path() / "modules" / "daemon" / "Release",
        fs::current_path().parent_path() / "modules" / "daemon" / "Debug",
    };

    fs::path cso_dir;
    for (const auto& cand : candidates) {
        std::error_code ec;
        if (fs::exists(cand / "passthrough_ps.cso", ec)) {
            cso_dir = cand;
            break;
        }
    }

    if (cso_dir.empty()) {
        std::printf("test_cso_compile: no compiled CSOs found in candidate paths, skipping\n");
        return 0;  // skip, not fail
    }

    std::printf("test_cso_compile: found CSOs at %ls\n", cso_dir.c_str());

    int found = 0;
    int missing = 0;
    for (const auto& name : kExpectedCsos) {
        fs::path cso = cso_dir / name;
        std::error_code ec;
        if (!fs::exists(cso, ec)) {
            std::printf("test_cso_compile: missing %s\n", name.c_str());
            ++missing;
            continue;
        }
        auto size = fs::file_size(cso, ec);
        if (size == 0) {
            std::printf("test_cso_compile: %s is zero bytes\n", name.c_str());
            ++missing;
            continue;
        }
        std::printf("test_cso_compile: %s = %llu bytes\n",
                    name.c_str(),
                    static_cast<unsigned long long>(size));
        ++found;
    }

    if (missing > 0) {
        std::printf("test_cso_compile: %d CSOs found, %d missing -> FAIL\n",
                    found, missing);
        return 1;
    }
    std::printf("test_cso_compile: OK (%d CSOs verified)\n", found);
    return 0;
}
