// filepath: modules/shaders/passthrough_ps.hlsl
// Real passthrough blit pixel shader for the overlay presentation path.
//
// Used when the daemon runs in passthrough mode or when CSO compute
// shaders are not yet available. The pipeline module binds this shader
// and the imported shared color SRV at slot t0, then draws a fullscreen
// triangle to the swap chain RTV.
//
// Bindings:
//   t0: Source color (DXGI_FORMAT_B8G8R8A8_UNORM, etc.)
//   s0: Linear sampler

Texture2D    SourceTex  : register(t0);
SamplerState LinearSampler : register(s0);

struct VsOut {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

VsOut VsMain(uint id : SV_VertexID) {
    VsOut o;
    // Fullscreen triangle covering NDC [-1, +1] without a vertex buffer.
    float2 ndc = float2((id == 2) ? 3.0 : -1.0,
                        (id == 1) ? 3.0 : -1.0);
    o.pos = float4(ndc, 0.0f, 1.0f);
    o.uv  = float2((ndc.x + 1.0f) * 0.5f, 1.0f - (ndc.y + 1.0f) * 0.5f);
    return o;
}

float4 PsMain(VsOut i) : SV_TARGET {
    return SourceTex.Sample(LinearSampler, i.uv);
}
