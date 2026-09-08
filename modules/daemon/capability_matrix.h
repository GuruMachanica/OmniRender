// filepath: modules/daemon/capability_matrix.h
// Renderer + capability detection.
//
// Two responsibilities:
//   1. Identify which graphics API a given executable uses (D3D9,
//      D3D10/11, OpenGL, Vulkan, D3D8 via d3d8to9, etc.) by
//      inspecting the PE import table of the .exe. This lets the
//      launcher pick the right hook DLL.
//   2. Probe the running GPU and pick an execution profile
//      (RTX_TEMPORAL, SPATIAL_LIGHTWEIGHT, or BASELINE_PASSTHROUGH).
//
// The PE import table inspection is dependency-free: it walks the
// IMAGE_DOS_HEADER -> IMAGE_NT_HEADERS -> IMAGE_OPTIONAL_HEADER ->
// import directory -> IMAGE_IMPORT_DESCRIPTOR array and compares each
// DLL name to a small lookup table.

#pragma once

#include <string>
#include <vector>

namespace omnirender::daemon {

// What the executable imports.
enum class RendererApi {
    Unknown = 0,
    D3D8,        // DirectX 8  (only via d3d8to9 wrapper)
    D3D9,        // DirectX 9
    D3D10,       // DirectX 10
    D3D11,       // DirectX 11
    D3D12,       // DirectX 12
    OpenGL,      // opengl32.dll
    Vulkan,      // vulkan-1.dll
    Software,    // gdi / null renderer
};

struct DetectedRenderers {
    std::vector<RendererApi> detected;
    std::vector<std::string> imported_dlls;
    // The "primary" renderer. Multi-renderer games are rare; if both
    // d3d11.dll and opengl32.dll are imported, we prefer the more
    // modern one (D3D12 > Vulkan > D3D11 > OpenGL > D3D9 > D3D8).
    RendererApi primary = RendererApi::Unknown;
};

// Inspect the .exe's import table. Returns the list of detected
// renderers and the primary one. On any error, returns an empty
// result.
DetectedRenderers InspectExecutable(const std::wstring& exe_path);

// Human-readable name of the API.
const char* RendererApiName(RendererApi api) noexcept;

// Returns true if the executable imports d3d9.dll (or a wrapper).
bool ImportsD3D9(const std::wstring& exe_path) noexcept;

// Returns true if the executable imports opengl32.dll.
bool ImportsOpenGL(const std::wstring& exe_path) noexcept;

// Returns true if the executable imports dxgi (D3D10/11/12).
bool ImportsDXGI(const std::wstring& exe_path) noexcept;

}  // namespace omnirender::daemon
