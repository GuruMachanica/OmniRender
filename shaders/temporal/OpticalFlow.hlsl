// OpticalFlow.hlsl
// Screen-space optical flow fallback (block search against committed history).
//
// This is the FALLBACK motion source (audit #20): it runs only when depth +
// camera reprojection cannot produce motion vectors — DXGI games publish no
// view_proj matrices and some GL games never touch the fixed-function matrix
// stacks. Output convention is IDENTICAL to MotionReproject.hlsl: NDC delta
// (current - previous), so DLSS/XeSS/disocclusion consume it unchanged.
//
// Quality contract: single-luma-tap SAD over a +-8 px window with a static
// early-out. That resolves ±1 px motion — good enough for neural
// reconstruction input — but fine-grained sub-pixel motion is coarser than
// true reprojection. Reprojection always wins when it can run.
//
// cbuffer layout matches core::temporal::ReprojectionCB (TemporalPassBase.h).
// g_PrevViewProj / g_InvViewProj are unused here. g_NearZ / g_FarZ are
// repurposed by OpticalFlowPass::ExecuteGpu to carry the HISTORY texture
// dimensions (width / height) so history-res lookups can be mapped into
// input-res motion coordinates (history lives at output resolution when
// upscaling is active).

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

Texture2D<float4>   t_CurrColor : register(t0);
Texture2D<float4>   t_PrevColor : register(t1);
RWTexture2D<float2> u_Motion    : register(u0);

// Must match OpticalFlowPass::search_radius default (the CPU fallback uses
// its own runtime-configurable radius).
static const uint kRadius = 8;

static const float3 kLuma = float3(0.2126, 0.7152, 0.0722);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    uint W = (uint)g_InputWidth;
    uint H = (uint)g_InputHeight;
    if (id.x >= W || id.y >= H) return;

    const float curr = dot(t_CurrColor[id.xy].rgb, kLuma);

    // History lookup at history resolution (g_NearZ/g_FarZ carry its dims;
    // see header comment). Per-axis nearest-texel mapping input -> history.
    const uint HW = max(1u, (uint)g_NearZ);
    const uint HH = max(1u, (uint)g_FarZ);
    const uint2 hxy = uint2(min(id.x * HW / W, HW - 1u), min(id.y * HH / H, HH - 1u));

    // Static-pixel early-out: if the center barely changed, motion is zero.
    // Most game pixels are static or sub-window per frame, so this bound
    // keeps the fallback's cost proportional to actual motion.
    const float prevCenter = dot(t_PrevColor[hxy].rgb, kLuma);
    if (abs(curr - prevCenter) < 0.004) {
        u_Motion[id.xy] = float2(0.0, 0.0);
        return;
    }

    float bestScore = 1e9;
    int2  best = int2(0, 0);
    // Search runs in INPUT-res space; each tap is sampled from history
    // through the same per-axis mapping, so `best` is directly comparable
    // across pixels and converts to NDC with the input dimensions.
    [loop]
    for (int dy = -kRadius; dy <= (int)kRadius; ++dy) {
        [loop]
        for (int dx = -kRadius; dx <= (int)kRadius; ++dx) {
            int2 s = clamp(int2(id.xy) + int2(dx, dy), int2(0, 0), int2(W - 1, H - 1));
            const uint2 hs = uint2(min((uint)s.x * HW / W, HW - 1u),
                                   min((uint)s.y * HH / H, HH - 1u));
            const float score = abs(dot(t_PrevColor[hs].rgb, kLuma) - curr);
            [branch]
            if (score < bestScore) {
                bestScore = score;
                best = int2(dx, dy);
            }
        }
    }

    // NDC motion, current -> previous (best is the offset where the previous
    // frame matches the current pixel, i.e. prev_pos - curr_pos), scaled by
    // 2 / size — exactly the MotionReproject.hlsl convention.
    u_Motion[id.xy] = float2(best) * 2.0 / float2(g_InputWidth, g_InputHeight);
}
