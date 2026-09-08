// Disocclusion.hlsl
// Depth-based disocclusion mask generation.
// Pixels whose reprojected previous depth differs significantly from current depth
// are marked as disoccluded (value = 1.0).
// Compile: fxc /T cs_5_0 /E CSMain /Fo Disocclusion.cso Disocclusion.hlsl

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
    float  g_Pad;
};

static const float kDepthThreshold = 0.02;

Texture2D<float>       t_CurrDepth : register(t0);
Texture2D<float>       t_PrevDepth : register(t1);
RWTexture2D<float>     u_Disocc    : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    uint W = (uint)g_InputWidth;
    uint H = (uint)g_InputHeight;
    if (id.x >= W || id.y >= H) return;

    float currDepth = t_CurrDepth[id.xy];

    // Reconstruct world position from current pixel.
    float ndcX =  (float(id.x) + 0.5) * g_RcpWidth  * 2.0 - 1.0;
    float ndcY = -(float(id.y) + 0.5) * g_RcpHeight * 2.0 + 1.0;
    float4 worldPos = mul(float4(ndcX, ndcY, currDepth, 1.0), g_InvViewProj);
    worldPos.xyz /= worldPos.w;

    // Project to previous frame to find the corresponding previous pixel.
    float4 prevClip = mul(float4(worldPos.xyz, 1.0), g_PrevViewProj);
    float2 prevNDC  = prevClip.xy / prevClip.w;
    float  prevDepth_reprojected = prevClip.z / prevClip.w;

    // Sample previous depth at reprojected location.
    float2 prevUV = prevNDC * float2(0.5, -0.5) + 0.5;
    int2   prevPx = int2(prevUV * float2(W, H));
    prevPx = clamp(prevPx, int2(0, 0), int2(W - 1, H - 1));
    float prevDepth_sampled = t_PrevDepth[prevPx];

    float delta = abs(prevDepth_reprojected - prevDepth_sampled);
    u_Disocc[id.xy] = (delta > kDepthThreshold) ? 1.0 : 0.0;
}
