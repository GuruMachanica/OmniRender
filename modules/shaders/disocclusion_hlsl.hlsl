// filepath: modules/shaders/disocclusion_hlsl.hlsl
// Disocclusion detection compute shader for OmniRender.
// Compares current linearized depth with reprojected historical depth
// to detect geometric edges and newly uncovered background pixels.
//
// Output: R8_UNORM, 0.0 = valid history (no disocclusion), 1.0 = disoccluded (reject history)
//
// Thread group size: 16x16x1
// Bindings:
//   t0: CurrentDepth       (R32F or R16F)
//   t1: PreviousDepth      (R32F or R16F)
//   t2: MotionVectors      (RG16F, NDC or pixel velocity)
//   u0: DisocclusionOutput (R8_UNORM)
//   b0: DisocclusionConstants

Texture2D<float>   CurrentDepth       : register(t0);
Texture2D<float>   PreviousDepth      : register(t1);
Texture2D<float2>  MotionVectors      : register(t2);
RWTexture2D<float> DisocclusionOutput : register(u0);

cbuffer DisocclusionConstants : register(b0) {
    float2 TexelSize;       // 1.0 / Width, 1.0 / Height
    float  DepthThreshold;  // Relative depth delta threshold (e.g. 0.03)
    float  MotionSensitivity;
    float  Width;
    float  Height;
    float  _Pad0;
    float  _Pad1;
};

[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID) {
    int2 coord = dispatchThreadId.xy;
    if (coord.x >= int(Width) || coord.y >= int(Height)) return;

    float curDepth = CurrentDepth.Load(int3(coord, 0));
    float2 motion  = MotionVectors.Load(int3(coord, 0));

    // Convert motion vector from NDC [-1, 1] to pixel offset
    float2 pixelVelocity = motion * float2(Width * 0.5f, -Height * 0.5f);
    int2 prevCoord = int2(round(float2(coord) - pixelVelocity));

    // Boundary check: if historical sample is outside screen, it's 100% disoccluded
    if (prevCoord.x < 0 || prevCoord.x >= int(Width) ||
        prevCoord.y < 0 || prevCoord.y >= int(Height)) {
        DisocclusionOutput[coord] = 1.0f;
        return;
    }

    float prevDepth = PreviousDepth.Load(int3(prevCoord, 0));

    // If historical depth is significantly in front of current depth, an occluder
    // moved away, revealing this background pixel -> Disocclusion!
    float depthDelta = (prevDepth - curDepth) / max(curDepth, 1e-4f);
    float disocclusion = smoothstep(0.0f, DepthThreshold, depthDelta);

    // Also check motion divergence with 3x3 neighborhood depth
    float minNeighborDepth = curDepth;
    [unroll]
    for (int dy = -1; dy <= 1; ++dy) {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx) {
            int2 nc = clamp(coord + int2(dx, dy), int2(0, 0), int2(int(Width) - 1, int(Height) - 1));
            minNeighborDepth = min(minNeighborDepth, CurrentDepth.Load(int3(nc, 0)));
        }
    }

    float edgeFactor = abs(curDepth - minNeighborDepth) / max(curDepth, 1e-4f);
    if (edgeFactor > DepthThreshold * 2.0f) {
        disocclusion = max(disocclusion, 0.75f);
    }

    DisocclusionOutput[coord] = saturate(disocclusion);
}
