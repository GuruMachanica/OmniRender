// filepath: modules/daemon/capability_matrix.cpp
// PE import-table inspector for renderer detection.

#include "capability_matrix.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// WIN32_LEAN_AND_MEAN is added globally via add_compile_definitions in
// the root CMakeLists.txt; the local #define is intentionally omitted
// here so MSVC's C4005 ('macro redefinition') stays quiet.
#include <windows.h>

namespace omnirender::daemon {

namespace {

// Lower-case an ASCII string in place.
std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Read the entire file into a byte buffer.
std::vector<uint8_t> ReadFile(const std::wstring& path) {
    std::ifstream in(std::filesystem::path(path), std::ios::binary);
    if (!in.is_open()) return {};
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
}

bool ContainsDll(const std::vector<std::string>& dlls, const char* name) {
    std::string target = ToLower(name);
    for (const auto& d : dlls) {
        if (d == target) return true;
    }
    return false;
}

}  // namespace

const char* RendererApiName(RendererApi api) noexcept {
    switch (api) {
        case RendererApi::D3D8:     return "D3D8";
        case RendererApi::D3D9:     return "D3D9";
        case RendererApi::D3D10:    return "D3D10";
        case RendererApi::D3D11:    return "D3D11";
        case RendererApi::D3D12:    return "D3D12";
        case RendererApi::OpenGL:   return "OpenGL";
        case RendererApi::Vulkan:   return "Vulkan";
        case RendererApi::Software: return "Software";
        case RendererApi::Unknown:
        default:                    return "Unknown";
    }
}

DetectedRenderers InspectExecutable(const std::wstring& exe_path) {
    DetectedRenderers out;
    if (exe_path.empty()) return out;

    std::vector<uint8_t> bytes = ReadFile(exe_path);
    if (bytes.size() < sizeof(IMAGE_DOS_HEADER)) return out;

    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return out;
    if (static_cast<size_t>(dos->e_lfanew) + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) > bytes.size()) {
        return out;
    }
    const uint8_t* nt_ptr = bytes.data() + dos->e_lfanew;
    auto sig = *reinterpret_cast<const DWORD*>(nt_ptr);
    if (sig != IMAGE_NT_SIGNATURE) return out;

    auto* file_header = reinterpret_cast<const IMAGE_FILE_HEADER*>(nt_ptr + sizeof(DWORD));
    bool is64 = (file_header->Machine == IMAGE_FILE_MACHINE_AMD64);

    DWORD import_rva = 0;
    const IMAGE_SECTION_HEADER* sections = nullptr;
    WORD num_sections = file_header->NumberOfSections;

    if (is64) {
        if (static_cast<size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > bytes.size()) return out;
        auto* nt64 = reinterpret_cast<const IMAGE_NT_HEADERS64*>(nt_ptr);
        import_rva = nt64->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
        sections = IMAGE_FIRST_SECTION(nt64);
    } else {
        if (static_cast<size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS32) > bytes.size()) return out;
        auto* nt32 = reinterpret_cast<const IMAGE_NT_HEADERS32*>(nt_ptr);
        import_rva = nt32->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
        sections = IMAGE_FIRST_SECTION(nt32);
    }

    if (import_rva == 0 || !sections) return out;

    if (reinterpret_cast<const uint8_t*>(sections + num_sections) > bytes.data() + bytes.size()) {
        return out;
    }

    // Walk import directory. IMAGE_SECTION_HEADER* to translate RVA -> file offset.
    auto rva_to_offset = [&](DWORD rva) -> size_t {
        for (WORD i = 0; i < num_sections; ++i) {
            const auto& s = sections[i];
            DWORD start = s.VirtualAddress;
            DWORD end   = start + std::max(s.Misc.VirtualSize, s.SizeOfRawData);
            if (rva >= start && rva < end) {
                return static_cast<size_t>(rva - start + s.PointerToRawData);
            }
        }
        return 0;
    };

    size_t import_offset = rva_to_offset(import_rva);
    if (import_offset == 0 || import_offset >= bytes.size()) return out;

    const auto* desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(
        bytes.data() + import_offset);
    while (reinterpret_cast<const uint8_t*>(desc + 1) <= bytes.data() + bytes.size() && desc->Name != 0) {
        size_t name_off = rva_to_offset(desc->Name);
        if (name_off != 0 && name_off < bytes.size()) {
            const char* name_ptr = reinterpret_cast<const char*>(bytes.data() + name_off);
            size_t max_len = bytes.size() - name_off;
            size_t len = strnlen(name_ptr, max_len);
            std::string n = ToLower(std::string(name_ptr, len));
            out.imported_dlls.push_back(n);
        }
        ++desc;
    }
    std::sort(out.imported_dlls.begin(), out.imported_dlls.end());
    out.imported_dlls.erase(
        std::unique(out.imported_dlls.begin(), out.imported_dlls.end()),
        out.imported_dlls.end());

    // Map DLL names to renderers.
    if (ContainsDll(out.imported_dlls, "d3d12.dll"))    out.detected.push_back(RendererApi::D3D12);
    if (ContainsDll(out.imported_dlls, "d3d11.dll"))    out.detected.push_back(RendererApi::D3D11);
    if (ContainsDll(out.imported_dlls, "d3d10.dll"))    out.detected.push_back(RendererApi::D3D10);
    if (ContainsDll(out.imported_dlls, "d3d9.dll"))     out.detected.push_back(RendererApi::D3D9);
    if (ContainsDll(out.imported_dlls, "d3d8.dll"))     out.detected.push_back(RendererApi::D3D8);
    if (ContainsDll(out.imported_dlls, "opengl32.dll")) out.detected.push_back(RendererApi::OpenGL);
    if (ContainsDll(out.imported_dlls, "vulkan-1.dll"))out.detected.push_back(RendererApi::Vulkan);
    if (ContainsDll(out.imported_dlls, "gdi32.dll") ||
        ContainsDll(out.imported_dlls, "gdi32full.dll")) {
        out.detected.push_back(RendererApi::Software);
    }

    // Pick the primary renderer (most modern first).
    for (auto api : {RendererApi::D3D12, RendererApi::Vulkan,
                     RendererApi::D3D11, RendererApi::D3D10,
                     RendererApi::OpenGL, RendererApi::D3D9,
                     RendererApi::D3D8, RendererApi::Software}) {
        if (std::find(out.detected.begin(), out.detected.end(), api)
            != out.detected.end()) {
            out.primary = api;
            break;
        }
    }
    if (out.detected.empty()) out.primary = RendererApi::Unknown;
    return out;
}

bool ImportsD3D9(const std::wstring& exe_path) noexcept {
    auto r = InspectExecutable(exe_path);
    return std::find(r.detected.begin(), r.detected.end(), RendererApi::D3D9)
        != r.detected.end();
}

bool ImportsOpenGL(const std::wstring& exe_path) noexcept {
    auto r = InspectExecutable(exe_path);
    return std::find(r.detected.begin(), r.detected.end(), RendererApi::OpenGL)
        != r.detected.end();
}

bool ImportsDXGI(const std::wstring& exe_path) noexcept {
    auto r = InspectExecutable(exe_path);
    return std::find(r.detected.begin(), r.detected.end(), RendererApi::D3D11)
        != r.detected.end() ||
           std::find(r.detected.begin(), r.detected.end(), RendererApi::D3D12)
        != r.detected.end();
}

}  // namespace omnirender::daemon
