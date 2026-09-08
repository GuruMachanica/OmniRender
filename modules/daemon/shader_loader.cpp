// filepath: modules/daemon/shader_loader.cpp
// D3D11 shader CSO discovery and bytecode loading.

#include "shader_loader.h"

#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>
#include <cwchar>

#include "../common/logging.h"

namespace omnirender::daemon {

namespace {

ID3DBlob* TryReadShaderBlob(const wchar_t* name) {
    if (!name) return nullptr;
    const wchar_t* bare_name = name;
    for (const wchar_t* p = name; *p; ++p) {
        if (*p == L'/' || *p == L'\\') bare_name = p + 1;
    }
    wchar_t exe_dir[MAX_PATH]{};
    wchar_t exe_shaders[MAX_PATH]{};
    if (::GetModuleFileNameW(nullptr, exe_dir, MAX_PATH) > 0) {
        wchar_t* last_slash = wcsrchr(exe_dir, L'\\');
        if (!last_slash) last_slash = wcsrchr(exe_dir, L'/');
        if (last_slash) {
            *(last_slash + 1) = L'\0';
            _snwprintf_s(exe_shaders, _TRUNCATE, L"%ls%ls", exe_dir, L"shaders\\");
        }
    }

    const wchar_t* paths[] = {
        name,
        exe_shaders,
        exe_dir,
        L"shaders/",
        L"bin_release/shaders/",
        L"modules/shaders/",
        L"build/shaders/",
        nullptr
    };
    for (size_t i = 0; paths[i] != nullptr; ++i) {
        wchar_t prefixed[MAX_PATH]{};
        const wchar_t* p = paths[i];
        size_t len = wcslen(p);
        if (len > 0 && (p[len - 1] == L'/' || p[len - 1] == L'\\')) {
            _snwprintf_s(prefixed, _TRUNCATE, L"%ls%ls", p, bare_name);
            p = prefixed;
        }
        FILE* f = nullptr;
        _wfopen_s(&f, p, L"rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (size <= 0) { fclose(f); continue; }
        ID3DBlob* blob = nullptr;
        if (SUCCEEDED(D3DCreateBlob(static_cast<SIZE_T>(size), &blob)) && blob) {
            if (fread(blob->GetBufferPointer(), 1, static_cast<size_t>(size), f) == static_cast<size_t>(size)) {
                fclose(f);
                return blob;
            }
            blob->Release();
        }
        fclose(f);
    }
    return nullptr;
}

bool TryLoadVertexShader(ID3D11Device* device, const wchar_t* name, ID3D11VertexShader** out) {
    if (!device || !name || !out) return false;
    if (*out) { (*out)->Release(); *out = nullptr; }
    ID3DBlob* blob = TryReadShaderBlob(name);
    if (!blob) return false;
    HRESULT hr = device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, out);
    blob->Release();
    return SUCCEEDED(hr) && *out != nullptr;
}

bool TryLoadPixelShader(ID3D11Device* device, const wchar_t* name, ID3D11PixelShader** out) {
    if (!device || !name || !out) return false;
    if (*out) { (*out)->Release(); *out = nullptr; }
    ID3DBlob* blob = TryReadShaderBlob(name);
    if (!blob) return false;
    HRESULT hr = device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, out);
    blob->Release();
    return SUCCEEDED(hr) && *out != nullptr;
}

static const char kPassthroughHLSL[] =
    "Texture2D SourceTex : register(t0);\n"
    "SamplerState LinearSampler : register(s0);\n"
    "struct VsOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };\n"
    "VsOut VsMain(uint id : SV_VertexID) {\n"
    "    VsOut o;\n"
    "    float2 ndc = float2((id == 2) ? 3.0 : -1.0, (id == 1) ? 3.0 : -1.0);\n"
    "    o.pos = float4(ndc, 0.0f, 1.0f);\n"
    "    o.uv = float2((ndc.x + 1.0f) * 0.5f, 1.0f - (ndc.y + 1.0f) * 0.5f);\n"
    "    return o;\n"
    "}\n"
    "float4 PsMain(VsOut i) : SV_TARGET {\n"
    "    return SourceTex.Sample(LinearSampler, i.uv);\n"
    "}\n";

}  // namespace

bool LoadComputeShader(ID3D11Device* device, const wchar_t* path, ID3D11ComputeShader** out) {
    if (!device || !path || !out) return false;
    if (*out) { (*out)->Release(); *out = nullptr; }
    ID3DBlob* blob = TryReadShaderBlob(path);
    if (!blob) return false;
    HRESULT hr = device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, out);
    blob->Release();
    return SUCCEEDED(hr) && *out != nullptr;
}

void EnsurePassthroughShaders(ID3D11Device* device, ID3D11VertexShader** vs_out, ID3D11PixelShader** ps_out) {
    if (!device) return;
    if (!*vs_out) TryLoadVertexShader(device, L"passthrough_vs.cso", vs_out);
    if (!*ps_out) TryLoadPixelShader(device, L"passthrough_ps.cso", ps_out);

    if (!*vs_out) {
        ID3DBlob* code = nullptr;
        if (SUCCEEDED(D3DCompile(kPassthroughHLSL, strlen(kPassthroughHLSL), "passthrough_vs",
                                 nullptr, nullptr, "VsMain", "vs_5_0", 0, 0, &code, nullptr)) && code) {
            device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, vs_out);
            code->Release();
        }
    }
    if (!*ps_out) {
        ID3DBlob* code = nullptr;
        if (SUCCEEDED(D3DCompile(kPassthroughHLSL, strlen(kPassthroughHLSL), "passthrough_ps",
                                 nullptr, nullptr, "PsMain", "ps_5_0", 0, 0, &code, nullptr)) && code) {
            device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, ps_out);
            code->Release();
        }
    }
}

}  // namespace omnirender::daemon
