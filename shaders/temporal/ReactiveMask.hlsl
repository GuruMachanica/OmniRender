// ReactiveMask.hlsl
// Heuristic reactive mask generation via luminance-delta between current color
// and the previous history frame.  High-delta pixels (particles, UI, transparents)
// are marked reactive so DLSS reduces ghosting on them.
//
// History is sampled via normalized UV so this shader is resolution-agnostic:
// current color and history color can differ in resolution without coordinate
// mis-mapping.
// Compile: fxc /T cs_5_0 /E CSMain /Fo ReactiveMask.cso ReactiveMask.hlsl

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

static const float kLuminanceThreshold = 0.15;
static const float3 kLumWeights = float3(0.2126, 0.7152, 0.0722);

Texture2D<float4>  t_CurrColor    : register(t0);
Texture2D<float4>  t_HistoryColor : register(t1);
RWTexture2D<float> u_Reactive     : register(u0);
SamplerState       s_Linear       : register(s0); // LinearClamp

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    uint W = (uint)g_InputWidth;
    uint H = (uint)g_InputHeight;
    if (id.x >= W || id.y >= H) return;

    float3 curr = t_CurrColor[id.xy].rgb;

    // Sample history with normalized UV so mismatched resolutions map correctly.
    // This handles the case where history is at output resolution (e.g. 2560x1440)
    // while current color is at input resolution (e.g. 1920x1080).
    float2 uv = (float2(id.xy) + 0.5) * float2(g_RcpWidth, g_RcpHeight);
    float3 history = t_HistoryColor.SampleLevel(s_Linear, uv, 0).rgb;

    float lumCurr    = dot(curr,    kLumWeights);
    float lumHistory = dot(history, kLumWeights);

    float delta = abs(lumCurr - lumHistory);
    // Soft threshold: smoothstep for a gradual ramp, not a hard binary mask.
    float reactive = smoothstep(kLuminanceThreshold, kLuminanceThreshold * 2.0, delta);

    u_Reactive[id.xy] = reactive;
}

