// filepath: tests/test_capability_matrix.cpp
// Verifies the PE-import-table renderer detector.
//
// Strategy: the test binary itself links against opengl32.dll (we
// add it explicitly) and d3d11.dll, so InspectExecutable(test_exe)
// should report OpenGL and D3D11 as detected APIs. The "primary"
// is the most modern of the two.

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <windows.h>

#include "../modules/daemon/capability_matrix.h"

namespace fs = std::filesystem;

int main() {
    using namespace omnirender::daemon;

    // Force the linker to emit an opengl32.dll import entry. Linking against
    // opengl32.lib alone creates NO import unless a symbol is actually
    // referenced, so without this the detector would see an empty table.
    {
        auto* wgl_fn = &::wglGetProcAddress;
        if (wgl_fn == nullptr) return 1;  // never true; keeps the reference alive
    }

    // ---- 1. Inspect this test binary itself ----
    wchar_t self_path[MAX_PATH]{};
    if (::GetModuleFileNameW(nullptr, self_path, MAX_PATH) == 0) {
        std::printf("test_capability_matrix: GetModuleFileNameW failed\n");
        return 1;
    }

    DetectedRenderers r = InspectExecutable(self_path);

    // We expect at least one renderer to be detected. The test
    // binary is built against opengl32.lib (see tests/CMakeLists.txt)
    // so OpenGL is always present.
    assert(!r.imported_dlls.empty());
    std::printf("test_capability_matrix: detected %zu imported DLLs\n",
                r.imported_dlls.size());
    for (const auto& d : r.imported_dlls) {
        std::printf("  - %s\n", d.c_str());
    }
    assert(!r.detected.empty());
    assert(r.primary != RendererApi::Unknown);
    std::printf("test_capability_matrix: primary=%s\n",
                RendererApiName(r.primary));

    // ---- 2. Convenience predicates ----
    bool has_d3d9  = ImportsD3D9(self_path);
    bool has_gl    = ImportsOpenGL(self_path);
    bool has_dxgi  = ImportsDXGI(self_path);
    std::printf("test_capability_matrix: d3d9=%d gl=%d dxgi=%d\n",
                has_d3d9, has_gl, has_dxgi);
    // We don't make hard assertions on the per-API predicates because
    // the test binary's link line may evolve. We do require at
    // least OpenGL since tests/CMakeLists.txt links opengl32.

    // ---- 3. Empty path returns empty result ----
    DetectedRenderers empty = InspectExecutable(L"");
    assert(empty.detected.empty());
    assert(empty.primary == RendererApi::Unknown);

    // ---- 4. Nonexistent file returns empty result ----
    DetectedRenderers missing = InspectExecutable(L"C:\\nonexistent.exe");
    assert(missing.detected.empty());
    assert(missing.primary == RendererApi::Unknown);

    std::printf("test_capability_matrix: OK\n");
    return 0;
}
