// filepath: modules/shaders/fsr_easu.hlsl
// AMD FidelityFX Super Resolution 1.0 - Edge-Adaptive Spatial Upsampling (EASU)
// HLSL Compute Shader (cs_5_0) for Direct3D 11.

Texture2D<float4> InputTexture : register(t0);
RWTexture2D<float4> OutputTexture : register(u0);

cbuffer FsrEasuConstants : register(b0) {
    float4 Const0; // { inputSize.x / outputSize.x, inputSize.y / outputSize.y, 0.5 * inputSize.x / outputSize.x - 0.5, 0.5 * inputSize.y / outputSize.y - 0.5 }
    float4 Const1; // { 1.0 / inputSize.x, 1.0 / inputSize.y, inputSize.x, inputSize.y }
    float4 Const2; // { outputSize.x, outputSize.y, 1.0 / outputSize.x, 1.0 / outputSize.y }
    float4 Const3; // padding
};

// Convert RGB to perceptual luma
float RGBToLuma(float3 rgb) {
    return dot(rgb, float3(0.299f, 0.587f, 0.114f));
}

// 2-lobe Lanczos-like windowed sinc approximation
float Lanczos2(float x) {
    if (abs(x) < 1e-5f) return 1.0f;
    float pi_x = 3.14159265f * x;
    return (sin(pi_x) / pi_x) * (sin(pi_x * 0.5f) / (pi_x * 0.5f));
}

[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint2 outCoord = dispatchThreadId.xy;
    if (outCoord.x >= (uint)Const2.x || outCoord.y >= (uint)Const2.y) {
        return;
    }

    // Map output pixel to input continuous coordinates
    float2 inPos = (float2(outCoord) + 0.5f) * Const0.xy - 0.5f;
    int2 baseCoord = (int2)floor(inPos);
    float2 subpix = inPos - float2(baseCoord);

    // Sample 12-tap cross-neighborhood
    //     [  ][ b][  ]
    // [ d][ e][ f][ g]
    // [ h][ i][ j][ k]
    //     [ l][  ][  ]
    float3 c_b = InputTexture.Load(int3(clamp(baseCoord + int2( 0, -1), int2(0, 0), (int2)Const1.zw - 1), 0)).rgb;
    float3 c_d = InputTexture.Load(int3(clamp(baseCoord + int2(-1,  0), int2(0, 0), (int2)Const1.zw - 1), 0)).rgb;
    float3 c_e = InputTexture.Load(int3(clamp(baseCoord + int2( 0,  0), int2(0, 0), (int2)Const1.zw - 1), 0)).rgb;
    float3 c_f = InputTexture.Load(int3(clamp(baseCoord + int2( 1,  0), int2(0, 0), (int2)Const1.zw - 1), 0)).rgb;
    float3 c_g = InputTexture.Load(int3(clamp(baseCoord + int2( 2,  0), int2(0, 0), (int2)Const1.zw - 1), 0)).rgb;
    float3 c_h = InputTexture.Load(int3(clamp(baseCoord + int2(-1,  1), int2(0, 0), (int2)Const1.zw - 1), 0)).rgb;
    float3 c_i = InputTexture.Load(int3(clamp(baseCoord + int2( 0,  1), int2(0, 0), (int2)Const1.zw - 1), 0)).rgb;
    float3 c_j = InputTexture.Load(int3(clamp(baseCoord + int2( 1,  1), int2(0, 0), (int2)Const1.zw - 1), 0)).rgb;
    float3 c_k = InputTexture.Load(int3(clamp(baseCoord + int2( 2,  1), int2(0, 0), (int2)Const1.zw - 1), 0)).rgb;
    float3 c_l = InputTexture.Load(int3(clamp(baseCoord + int2( 0,  2), int2(0, 0), (int2)Const1.zw - 1), 0)).rgb;

    // Compute luma for edge gradient estimation
    float b = RGBToLuma(c_b);
    float d = RGBToLuma(c_d);
    float e = RGBToLuma(c_e);
    float f = RGBToLuma(c_f);
    float g = RGBToLuma(c_g);
    float h = RGBToLuma(c_h);
    float i = RGBToLuma(c_i);
    float j = RGBToLuma(c_j);
    float k = RGBToLuma(c_k);
    float l = RGBToLuma(c_l);

    // Directional derivatives
    float dirX = (f - d) + (j - h) * 0.5f;
    float dirY = (i - b) + (j - f) * 0.5f;
    float len = sqrt(dirX * dirX + dirY * dirY);
    float2 dir = (len > 1e-4f) ? float2(dirX, dirY) / len : float2(0.0f, 1.0f);

    // Edge-adaptive weights along dominant gradient
    float w_e = Lanczos2(length(subpix - float2(0.0f, 0.0f)));
    float w_f = Lanczos2(length(subpix - float2(1.0f, 0.0f)));
    float w_i = Lanczos2(length(subpix - float2(0.0f, 1.0f)));
    float w_j = Lanczos2(length(subpix - float2(1.0f, 1.0f)));

    // Directional elongation
    float w_b = Lanczos2(length(subpix - float2(0.0f, -1.0f))) * 0.25f;
    float w_l = Lanczos2(length(subpix - float2(0.0f,  2.0f))) * 0.25f;
    float w_d = Lanczos2(length(subpix - float2(-1.0f, 0.0f))) * 0.25f;
    float w_g = Lanczos2(length(subpix - float2( 2.0f, 0.0f))) * 0.25f;

    float3 colorSum = c_e * w_e + c_f * w_f + c_i * w_i + c_j * w_j +
                      c_b * w_b + c_l * w_l + c_d * w_d + c_g * w_g;
    float weightSum = w_e + w_f + w_i + w_j + w_b + w_l + w_d + w_g;

    float3 result = (weightSum > 1e-4f) ? (colorSum / weightSum) : c_e;

    // Clamp to local neighborhood bounds to prevent ringing
    float3 minColor = min(min(c_e, c_f), min(c_i, c_j));
    float3 maxColor = max(max(c_e, c_f), max(c_i, c_j));
    result = clamp(result, minColor, maxColor);

    OutputTexture[outCoord] = float4(result, 1.0f);
}
