// MotionReproject.hlsl
// Depth-reprojected motion vector generation.
// Compile: fxc /T cs_5_0 /E CSMain /Fo MotionReproject.cso MotionReproject.hlsl

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

Texture2D<float>       t_Depth  : register(t0);
RWTexture2D<float2>    u_Motion : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    uint W = (uint)g_InputWidth;
    uint H = (uint)g_InputHeight;
    if (id.x >= W || id.y >= H) return;

    float depth = t_Depth[id.xy];

    // Reconstruct NDC position.
    float ndcX =  (float(id.x) + 0.5) * g_RcpWidth  * 2.0 - 1.0;
    float ndcY = -(float(id.y) + 0.5) * g_RcpHeight * 2.0 + 1.0;
    float ndcZ = depth;

    // Unproject to world space.
    float4 worldPos = mul(float4(ndcX, ndcY, ndcZ, 1.0), g_InvViewProj);
    worldPos.xyz /= worldPos.w;

    // Project to previous frame clip space.
    float4 prevClip = mul(float4(worldPos.xyz, 1.0), g_PrevViewProj);
    float2 prevNDC  = prevClip.xy / prevClip.w;

    // Motion vector = current NDC - previous NDC.
    u_Motion[id.xy] = float2(ndcX - prevNDC.x, ndcY - prevNDC.y);
}
