// filepath: modules/shaders/fsr_rcas.hlsl
// AMD FidelityFX Super Resolution 1.0 - Robust Contrast-Adaptive Sharpening (RCAS)
// HLSL Compute Shader (cs_5_0) for Direct3D 11.

Texture2D<float4> InputTexture : register(t0);
RWTexture2D<float4> OutputTexture : register(u0);

cbuffer FsrRcasConstants : register(b0) {
    float4 RcasConfig; // { Sharpness, Width, Height, Pad }
};

float RGBToLuma(float3 rgb) {
    return dot(rgb, float3(0.299f, 0.587f, 0.114f));
}

[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint2 coord = dispatchThreadId.xy;
    uint width = (uint)RcasConfig.y;
    uint height = (uint)RcasConfig.z;

    if (coord.x >= width || coord.y >= height) {
        return;
    }

    int2 c = int2(coord);
    int2 maxCoord = int2(width - 1, height - 1);

    // 5-tap cross
    //      [ b ]
    //  [ d ][ e ][ f ]
    //      [ h ]
    float3 b = InputTexture.Load(int3(clamp(c + int2( 0, -1), int2(0, 0), maxCoord), 0)).rgb;
    float3 d = InputTexture.Load(int3(clamp(c + int2(-1,  0), int2(0, 0), maxCoord), 0)).rgb;
    float3 e = InputTexture.Load(int3(c, 0)).rgb;
    float3 f = InputTexture.Load(int3(clamp(c + int2( 1,  0), int2(0, 0), maxCoord), 0)).rgb;
    float3 h = InputTexture.Load(int3(clamp(c + int2( 0,  1), int2(0, 0), maxCoord), 0)).rgb;

    float b_luma = RGBToLuma(b);
    float d_luma = RGBToLuma(d);
    float e_luma = RGBToLuma(e);
    float f_luma = RGBToLuma(f);
    float h_luma = RGBToLuma(h);

    // Min and max luma of 4-neighborhood
    float minLuma = min(min(b_luma, d_luma), min(f_luma, h_luma));
    float maxLuma = max(max(b_luma, d_luma), max(f_luma, h_luma));

    // Contrast limits to prevent ringing
    float nz = 0.25f * (b_luma + d_luma + f_luma + h_luma) - e_luma;
    float sharpness = clamp(RcasConfig.x, 0.0f, 1.0f);

    // Adaptive sharpening weight
    float hitMin = minLuma / (4.0f * max(maxLuma, 1e-4f));
    float hitMax = (1.0f - maxLuma) / (4.0f * max(1.0f - minLuma, 1e-4f));
    float lobe = max(-0.25f, -min(hitMin, hitMax) * sharpness);

    // Apply sharpening
    float3 result = (e + (b + d + f + h) * lobe) / (1.0f + 4.0f * lobe);

    // Clamp within neighborhood limits
    float3 minColor = min(min(b, d), min(f, h));
    float3 maxColor = max(max(b, d), max(f, h));
    result = clamp(result, minColor * 0.9f, maxColor * 1.1f);

    OutputTexture[coord] = float4(result, 1.0f);
}
