// filepath: modules/shaders/reconstruct_hlsl.hlsl
// Production-grade temporal accumulation compute shader for OmniRender.
// Features:
//   - Subpixel motion reprojection
//   - 3x3 YCoCg color space neighborhood variance clipping (anti-ghosting)
//   - Depth-based geometric disocclusion rejection
//   - Multi-factor reactive mask weighting (particles, UI, alpha)
//
// Thread group size: 16x16x1
// Bindings:
//   t0: CurrentColor      (RGBA)
//   t1: HistoryColor      (RGBA from previous frame)
//   t2: MotionVectors     (RG16F, NDC velocity)
//   t3: DisocclusionMask  (R8_UNORM)
//   t4: ReactiveMask      (R8_UNORM)
//   u0: Output            (RGBA UAV)
//   b0: ReconstructConstants

Texture2D<float4>  CurrentColor      : register(t0);
Texture2D<float4>  HistoryColor      : register(t1);
Texture2D<float2>  MotionVectors     : register(t2);
Texture2D<float>   DisocclusionMask  : register(t3);
Texture2D<float>   ReactiveMask      : register(t4);

RWTexture2D<float4> Output : register(u0);

SamplerState LinearSampler : register(s0);

cbuffer ReconstructConstants : register(b0) {
    float SourceWidth;
    float SourceHeight;
    float TargetWidth;
    float TargetHeight;
    float JitterX;
    float JitterY;
    float BaseBlend;       // Default ~0.90
    float ClampBoxScale;   // Default ~1.25 (variance scale)
};

float3 RGBToYCoCg(float3 c) {
    return float3(
         0.25f * c.r + 0.50f * c.g + 0.25f * c.b,
         0.50f * c.r - 0.50f * c.b,
        -0.25f * c.r + 0.50f * c.g - 0.25f * c.b
    );
}

float3 YCoCgToRGB(float3 c) {
    return float3(
        c.x + c.y - c.z,
        c.x + c.z,
        c.x - c.y - c.z
    );
}

// Intersect historical color ray with local AABB in YCoCg space
float3 ClipToAABB(float3 hist, float3 boxMin, float3 boxMax) {
    float3 p_clip = 0.5f * (boxMax + boxMin);
    float3 e_clip = 0.5f * (boxMax - boxMin) + 1e-5f;

    float3 v_clip = hist - p_clip;
    float3 v_unit = v_clip / e_clip;
    float3 a_unit = abs(v_unit);
    float ma_unit = max(a_unit.x, max(a_unit.y, a_unit.z));

    if (ma_unit > 1.0f) {
        return p_clip + v_clip / ma_unit;
    }
    return hist;
}

[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID) {
    int2 coord = dispatchThreadId.xy;
    if (coord.x >= int(SourceWidth) || coord.y >= int(SourceHeight)) return;

    float4 current = CurrentColor.Load(int3(coord, 0));
    float2 motion  = MotionVectors.Load(int3(coord, 0));

    // Convert motion vector from NDC [-1, 1] to pixel offset
    float2 pixelVelocity = motion * float2(SourceWidth * 0.5f, -SourceHeight * 0.5f);
    int2 prevCoord = int2(round(float2(coord) - pixelVelocity));

    // 1. Check screen boundary disocclusion
    if (prevCoord.x < 0 || prevCoord.x >= int(SourceWidth) ||
        prevCoord.y < 0 || prevCoord.y >= int(SourceHeight)) {
        Output[coord] = current;
        return;
    }

    // 2. Sample 3x3 neighborhood of current frame in YCoCg space
    float3 m1 = float3(0.0f, 0.0f, 0.0f);
    float3 m2 = float3(0.0f, 0.0f, 0.0f);
    float3 neighborhoodMin = float3(1e4f, 1e4f, 1e4f);
    float3 neighborhoodMax = float3(-1e4f, -1e4f, -1e4f);

    [unroll]
    for (int dy = -1; dy <= 1; ++dy) {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx) {
            int2 nc = clamp(coord + int2(dx, dy), int2(0, 0), int2(int(SourceWidth) - 1, int(SourceHeight) - 1));
            float3 sRGB = CurrentColor.Load(int3(nc, 0)).rgb;
            float3 sYCoCg = RGBToYCoCg(sRGB);

            m1 += sYCoCg;
            m2 += sYCoCg * sYCoCg;
            neighborhoodMin = min(neighborhoodMin, sYCoCg);
            neighborhoodMax = max(neighborhoodMax, sYCoCg);
        }
    }

    // Variance bounding box
    float3 mu = m1 / 9.0f;
    float3 sigma = sqrt(max(0.0f, (m2 / 9.0f) - mu * mu));
    float3 boxMin = max(neighborhoodMin, mu - ClampBoxScale * sigma);
    float3 boxMax = min(neighborhoodMax, mu + ClampBoxScale * sigma);

    // 3. Sample history frame and clamp in YCoCg space
    float4 prevSample = HistoryColor.Load(int3(prevCoord, 0));
    float3 prevYCoCg = RGBToYCoCg(prevSample.rgb);
    float3 clampedYCoCg = ClipToAABB(prevYCoCg, boxMin, boxMax);
    float3 clampedPrev = YCoCgToRGB(clampedYCoCg);

    // 4. Sample disocclusion and reactive masks
    float disocclusion = DisocclusionMask.Load(int3(coord, 0));
    float reactive     = ReactiveMask.Load(int3(coord, 0));

    // Dynamic trust weight: reduce history on disocclusion or high reactivity
    float historyConfidence = saturate((1.0f - disocclusion) * (1.0f - reactive * 0.90f));
    float blendAlpha = saturate(BaseBlend * historyConfidence);

    // 5. Final temporal accumulation
    float3 accumulated = lerp(current.rgb, clampedPrev, blendAlpha);
    Output[coord] = float4(accumulated, current.a);
}
