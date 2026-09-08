// filepath: modules/shaders/reactive_mask_hlsl.hlsl
// Reactive mask: identifies pixels where the historical color sample
// should NOT be trusted.
//
// This is the audit #22 "reactive mask" — particles, fire, smoke,
// UI, transparency, foliage. Conservative thresholds so we never
// reject history on a clean surface.
//
// Output: R8_UNORM, 0.0 = trust history, 1.0 = discard history.
//
// Thread group size: 16x16x1
// Bindings:
//   t0: CurrentColor   (RGBA8 or RGBA16F)
//   t1: PreviousColor  (same format as CurrentColor)
//   t2: LinearDepth    (R32F)
//   u0: ReactiveOutput (R8_UNORM)
//   b0: ReactiveConstants

Texture2D<float4> CurrentColor   : register(t0);
Texture2D<float4> PreviousColor  : register(t1);
Texture2D<float>  LinearDepth    : register(t2);
RWTexture2D<float> ReactiveOutput : register(u0);

cbuffer ReactiveConstants : register(b0) {
    float2 TexelSize;       // 1.0 / Width, 1.0 / Height
    float  ColorThreshold;   // luminance difference that triggers reactivity
    float  DepthThreshold;   // depth-difference ratio that triggers reactivity
    float  Scale;            // overall mask gain (0 = passthrough, 1 = full)
    float  _Pad0;
    float  _Pad1;
    float  _Pad2;
};

float Luma(float3 c) {
    return dot(c, float3(0.2126f, 0.7152f, 0.0722f));
}

[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID) {
    int2 coord = dispatchThreadId.xy;

    float3 cur  = CurrentColor.Load(int3(coord, 0)).rgb;
    float3 prev = PreviousColor.Load(int3(coord, 0)).rgb;
    float  zd   = LinearDepth.Load(int3(coord, 0));

    // 3x3 neighborhood for stable statistics.
    float3 minc = float3(1.0f, 1.0f, 1.0f);
    float3 maxc = float3(0.0f, 0.0f, 0.0f);
    [unroll]
    for (int dy = -1; dy <= 1; ++dy) {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx) {
            int2 nc = coord + int2(dx, dy);
            float3 s = PreviousColor.Load(int3(nc, 0)).rgb;
            minc = min(minc, s);
            maxc = max(maxc, s);
        }
    }

    float lumaCur  = Luma(cur);
    float lumaPrev = Luma(prev);

    // Color change: how much the current pixel disagrees with the
    // previous color, normalized by the neighborhood variation.
    float colorDiff = abs(lumaCur - lumaPrev) /
                      max(0.001f, Luma(maxc - minc));

    // Depth change: large depth differences in consecutive frames
    // usually mean a particle / explosion / fast object.
    float zNeighbor = LinearDepth.Load(int3(coord + int2(0, 1), 0));
    float depthDiff = (zNeighbor > 0.0f) ? abs(zd - zNeighbor) / max(0.01f, zd) : 0.0f;

    float reactive = saturate(
        max(colorDiff - ColorThreshold, 0.0f) +
        max(depthDiff - DepthThreshold, 0.0f)
    ) * Scale;

    ReactiveOutput[coord] = reactive;
}
