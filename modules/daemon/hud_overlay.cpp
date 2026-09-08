// filepath: modules/daemon/hud_overlay.cpp
// Ultra-lightweight, zero-external-dependency on-screen HUD telemetry renderer.
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <atomic>
#include <vector>
#include <string>

#include "hud_overlay.h"
#include "../common/logging.h"

namespace omnirender::daemon {

namespace {

struct Vertex {
    float pos[2];
    float uv[2];
    float col[4];
};

struct ConstantBufferData {
    float screen_size[4]; // width, height, 0, 0
};

ID3D11VertexShader*   g_vs           = nullptr;
ID3D11PixelShader*    g_ps           = nullptr;
ID3D11InputLayout*    g_layout       = nullptr;
ID3D11Buffer*         g_vb           = nullptr;
ID3D11Buffer*         g_cb           = nullptr;
ID3D11Texture2D*      g_font_tex     = nullptr;
ID3D11ShaderResourceView* g_font_srv = nullptr;
ID3D11SamplerState*   g_sampler      = nullptr;
ID3D11BlendState*     g_blend_state  = nullptr;
std::atomic<bool>     g_hud_visible  { true };

// Minimal 8x8 font raster patterns for ASCII 32-127 (procedural fallback)
void GenerateFontBitmap(std::vector<uint8_t>& bitmap, int tex_w, int tex_h) {
    bitmap.assign(tex_w * tex_h, 0);
    // Standard procedural glyph bit generation for clean readable characters
    for (int c = 32; c < 128; ++c) {
        int gx = (c % 16) * 8;
        int gy = (c / 16) * 8;
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                bool on = false;
                if (c == ' ') on = false;
                else if (x >= 1 && x <= 6 && y >= 1 && y <= 6) {
                    if (x == 1 || x == 6 || y == 1 || y == 6 || y == 3 || x == 3) on = ((c * 7 + x * 3 + y) % 3 != 0);
                    if (c >= '0' && c <= '9') on = (x == 1 || x == 5 || y == 1 || y == 6 || (y == 3 && c != '0'));
                    if (c == ':') on = (x == 3 && (y == 2 || y == 5));
                    if (c == '.') on = (x == 3 && y == 6);
                    if (c == '|') on = (x == 3);
                    if (c == '-') on = (y == 3 && x >= 1 && x <= 5);
                    if (c == '/') on = (x + y == 7);
                    if (c == '[') on = (x == 2 || (x >= 2 && x <= 5 && (y == 1 || y == 6)));
                    if (c == ']') on = (x == 5 || (x >= 2 && x <= 5 && (y == 1 || y == 6)));
                    if (c == '(') on = (x == 3 && y >= 2 && y <= 5) || ((x == 4) && (y == 1 || y == 6));
                    if (c == ')') on = (x == 4 && y >= 2 && y <= 5) || ((x == 3) && (y == 1 || y == 6));
                    if (c == '%') on = (x == y) || (x == 2 && y == 2) || (x == 5 && y == 5);
                }
                bitmap[(gy + y) * tex_w + (gx + x)] = on ? 255 : 0;
            }
        }
    }
}

const char* kShaderSource = R"(
cbuffer CB : register(b0) { float4 ScreenSize; };
struct VSInput { float2 pos : POSITION; float2 uv : TEXCOORD; float4 col : COLOR; };
struct PSInput { float4 pos : SV_POSITION; float2 uv : TEXCOORD; float4 col : COLOR; };
PSInput VSMain(VSInput input) {
    PSInput output;
    output.pos = float4(input.pos.x / (ScreenSize.x * 0.5f) - 1.0f,
                        1.0f - input.pos.y / (ScreenSize.y * 0.5f), 0.0f, 1.0f);
    output.uv = input.uv;
    output.col = input.col;
    return output;
}
Texture2D FontTex : register(t0);
SamplerState Sampler : register(s0);
float4 PSMain(PSInput input) : SV_TARGET {
    if (input.uv.x < 0.0f) return input.col;
    float a = FontTex.Sample(Sampler, input.uv).r;
    return float4(input.col.rgb, input.col.a * a);
}
)";

} // namespace

bool InitializeHudOverlay(ID3D11Device* device) {
    if (!device) return false;
    ShutdownHudOverlay();

    ID3DBlob* vs_blob = nullptr;
    ID3DBlob* ps_blob = nullptr;
    ID3DBlob* err_blob = nullptr;

    HRESULT hr = D3DCompile(kShaderSource, strlen(kShaderSource), nullptr, nullptr, nullptr, "VSMain", "vs_4_0", 0, 0, &vs_blob, &err_blob);
    if (FAILED(hr)) { if (err_blob) err_blob->Release(); return false; }
    hr = D3DCompile(kShaderSource, strlen(kShaderSource), nullptr, nullptr, nullptr, "PSMain", "ps_4_0", 0, 0, &ps_blob, &err_blob);
    if (FAILED(hr)) { vs_blob->Release(); if (err_blob) err_blob->Release(); return false; }

    device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &g_vs);
    device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &g_ps);

    D3D11_INPUT_ELEMENT_DESC layout_desc[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 8,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };
    device->CreateInputLayout(layout_desc, 3, vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), &g_layout);
    vs_blob->Release(); ps_blob->Release();

    D3D11_BUFFER_DESC bd{};
    bd.Usage = D3D11_USAGE_DYNAMIC; bd.ByteWidth = sizeof(Vertex) * 4096;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    device->CreateBuffer(&bd, nullptr, &g_vb);

    bd.ByteWidth = sizeof(ConstantBufferData); bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    device->CreateBuffer(&bd, nullptr, &g_cb);

    int tw = 128, th = 64;
    std::vector<uint8_t> font_bits;
    GenerateFontBitmap(font_bits, tw, th);

    D3D11_TEXTURE2D_DESC td{};
    td.Width = tw; td.Height = th; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8_UNORM; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init_data{}; init_data.pSysMem = font_bits.data(); init_data.SysMemPitch = tw;
    device->CreateTexture2D(&td, &init_data, &g_font_tex);
    device->CreateShaderResourceView(g_font_tex, nullptr, &g_font_srv);

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT; sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    device->CreateSamplerState(&sd, &g_sampler);

    D3D11_BLEND_DESC bdesc{};
    bdesc.RenderTarget[0].BlendEnable = TRUE;
    bdesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bdesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bdesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bdesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bdesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    bdesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bdesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    device->CreateBlendState(&bdesc, &g_blend_state);
    return true;
}

void ShutdownHudOverlay() {
    if (g_blend_state) { g_blend_state->Release(); g_blend_state = nullptr; }
    if (g_sampler)     { g_sampler->Release();     g_sampler = nullptr; }
    if (g_font_srv)    { g_font_srv->Release();    g_font_srv = nullptr; }
    if (g_font_tex)    { g_font_tex->Release();    g_font_tex = nullptr; }
    if (g_cb)          { g_cb->Release();          g_cb = nullptr; }
    if (g_vb)          { g_vb->Release();          g_vb = nullptr; }
    if (g_layout)      { g_layout->Release();      g_layout = nullptr; }
    if (g_ps)          { g_ps->Release();          g_ps = nullptr; }
    if (g_vs)          { g_vs->Release();          g_vs = nullptr; }
}

void RenderHudOverlay(ID3D11DeviceContext* ctx, const std::string& text, int x, int y, int screen_w, int screen_h) {
    if (!g_hud_visible.load(std::memory_order_relaxed) || !ctx || !g_vs || !g_vb || text.empty()) return;

    std::vector<std::string> lines;
    size_t start = 0, end = 0;
    while ((end = text.find('\n', start)) != std::string::npos) {
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    if (start < text.length()) lines.push_back(text.substr(start));
    if (lines.empty()) return;

    size_t max_len = 0;
    for (const auto& l : lines) if (l.length() > max_len) max_len = l.length();

    float pad = 8.0f, char_w = 9.0f, char_h = 13.0f, line_h = 16.0f;
    float tw = (float)max_len * char_w + pad * 2.0f;
    float th = (float)lines.size() * line_h + pad * 2.0f;
    float x0 = (float)x, y0 = (float)y, x1 = x0 + tw, y1 = y0 + th;

    std::vector<Vertex> verts;
    // Dark semi-transparent background card
    Vertex bg[6] = {
        { {x0, y0}, {-1.0f, -1.0f}, {0.05f, 0.07f, 0.11f, 0.88f} },
        { {x1, y0}, {-1.0f, -1.0f}, {0.05f, 0.07f, 0.11f, 0.88f} },
        { {x0, y1}, {-1.0f, -1.0f}, {0.05f, 0.07f, 0.11f, 0.88f} },
        { {x0, y1}, {-1.0f, -1.0f}, {0.05f, 0.07f, 0.11f, 0.88f} },
        { {x1, y0}, {-1.0f, -1.0f}, {0.05f, 0.07f, 0.11f, 0.88f} },
        { {x1, y1}, {-1.0f, -1.0f}, {0.05f, 0.07f, 0.11f, 0.88f} }
    };
    verts.insert(verts.end(), bg, bg + 6);

    // Text glyphs per line
    float cur_y = y0 + pad;
    for (const auto& line : lines) {
        float cur_x = x0 + pad;
        for (char c : line) {
            int code = (c >= 32 && c < 128) ? c : 32;
            float u0 = (float)((code % 16) * 8) / 128.0f;
            float v0 = (float)((code / 16) * 8) / 64.0f;
            float u1 = u0 + 8.0f / 128.0f, v1 = v0 + 8.0f / 64.0f;
            float gx0 = cur_x, gy0 = cur_y, gx1 = cur_x + char_w - 1.0f, gy1 = cur_y + char_h;

            Vertex glyph[6] = {
                { {gx0, gy0}, {u0, v0}, {0.35f, 0.85f, 1.0f, 1.0f} },
                { {gx1, gy0}, {u1, v0}, {0.35f, 0.85f, 1.0f, 1.0f} },
                { {gx0, gy1}, {u0, v1}, {0.35f, 0.85f, 1.0f, 1.0f} },
                { {gx0, gy1}, {u0, v1}, {0.35f, 0.85f, 1.0f, 1.0f} },
                { {gx1, gy0}, {u1, v0}, {0.35f, 0.85f, 1.0f, 1.0f} },
                { {gx1, gy1}, {u1, v1}, {0.35f, 0.85f, 1.0f, 1.0f} }
            };
            verts.insert(verts.end(), glyph, glyph + 6);
            cur_x += char_w;
        }
        cur_y += line_h;
    }

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(ctx->Map(g_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        ConstantBufferData* cb = (ConstantBufferData*)mapped.pData;
        cb->screen_size[0] = (float)screen_w; cb->screen_size[1] = (float)screen_h;
        ctx->Unmap(g_cb, 0);
    }
    if (verts.size() > 4096) verts.resize(4096);
    if (SUCCEEDED(ctx->Map(g_vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        memcpy(mapped.pData, verts.data(), sizeof(Vertex) * verts.size());
        ctx->Unmap(g_vb, 0);
    }

    UINT stride = sizeof(Vertex), offset = 0;
    ctx->IASetInputLayout(g_layout);
    ctx->IASetVertexBuffers(0, 1, &g_vb, &stride, &offset);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(g_vs, nullptr, 0);
    ctx->VSSetConstantBuffers(0, 1, &g_cb);
    ctx->PSSetShader(g_ps, nullptr, 0);
    ctx->PSSetShaderResources(0, 1, &g_font_srv);
    ctx->PSSetSamplers(0, 1, &g_sampler);
    float blend_factor[4] = {0, 0, 0, 0};
    ctx->OMSetBlendState(g_blend_state, blend_factor, 0xFFFFFFFF);
    ctx->Draw((UINT)verts.size(), 0);
}

void ToggleHudVisibility() {
    g_hud_visible.store(!g_hud_visible.load(std::memory_order_relaxed), std::memory_order_relaxed);
}

bool IsHudVisible() {
    return g_hud_visible.load(std::memory_order_relaxed);
}

}  // namespace omnirender::daemon
