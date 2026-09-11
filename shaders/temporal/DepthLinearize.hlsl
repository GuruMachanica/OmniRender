// DepthLinearize.hlsl
// Raw game depth -> linearized [0,1] view-depth.
//
// Consumed by core::temporal::DepthProvider as the optional GPU accelerator.
// The cbuffer layout must match core::temporal::ReprojectionCB exactly
// (see core/temporal/TemporalPassBase.h). The final float of the cbuffer
// (called g_Pad in the other temporal shaders) carries the reversed-Z flag:
//   0.0 = standard Z (near=0 maps to raw 0)
//   1.0 = reversed Z (near=0 maps to raw 1)
//
// Input is the shared R32F depth texture the hooks publish (raw NDC/hardware
// depth in the x channel). Output is depth linear in view space, normalized
// to [0,1] between the camera near and far planes:
//   viewZ   = n*f / (f - raw*(f-n))
//   linear  = (viewZ - n) / (f - n)
//
// Compile: dxc -T cs_5_0 -E CSMain -Fo DepthLinearize.cso DepthLinearize.hlsl

cbuffer ReprojectionCB : register(b0) {
    row_major float4x4 g_PrevViewProj;
    row_major float4x4 g_InvViewProj;
    float  g_InputWidth;
    float  g_InputHeight;
    float  g_RcpWidth;
    float  g_RcpHeight;
    float  g_NearZ;
    float  g_FarZ;
    uint   g_FrameIndex;
    float  g_ReverseZ;   // 0 = standard Z, 1 = reversed Z (see above)
};

Texture2D<float4>   t_Depth  : register(t0);
RWTexture2D<float>  u_Linear : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    uint W, H;
    u_Linear.GetDimensions(W, H);
    if (id.x >= W || id.y >= H) return;

    float raw = t_Depth[id.xy].x;
    raw = saturate(raw);

    // Reversed-Z: flip into the standard-Z domain first, then linearize once.
    if (g_ReverseZ > 0.5) raw = 1.0 - raw;

    const float n = max(g_NearZ, 1e-6);
    const float f = max(g_FarZ, n + 1e-6);

    // Perspective linearization (standard-Z projection).
    const float viewZ  = (n * f) / max(f - raw * (f - n), 1e-6);
    const float linear = saturate((viewZ - n) / (f - n));

    u_Linear[id.xy] = linear;
}
