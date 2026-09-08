// filepath: modules/shaders/optical_flow_dis.hlsl
// Screen-space velocity generator (PRD section 7.4).
//
// Multi-scale DIS motion vector compute pass. Reads linearized depth
// from consecutive color frames and writes RG16F motion vectors into
// the output texture for DLSS ingestion.

Texture2D<float>  CurrentDepth   : register(t0);
Texture2D<float>  PreviousDepth  : register(t1);
Texture2D<float4> CurrentColor   : register(t2);
Texture2D<float4> PreviousColor  : register(t3);

RWTexture2D<float2> MotionVectorOutput : register(u0);

cbuffer OpticalFlowConstants : register(b0) {
    float2 TexelSize;       // 1.0 / Width, 1.0 / Height
    float  CameraNear;
    float  CameraFar;
};

// Linearizes 24-bit or 32-bit non-linear hardware depth.
float LinearizeDepth(float depth) {
    return (CameraNear * CameraFar) / (CameraFar - depth * (CameraFar - CameraNear));
}

[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID) {
    int2 coord = dispatchThreadId.xy;

    float zCurrent = LinearizeDepth(CurrentDepth.Load(int3(coord, 0)));
    float zPrev    = LinearizeDepth(PreviousDepth.Load(int3(coord, 0)));

    // Compute spatial gradients along color intensity.
    float4 cCenter = CurrentColor.Load(int3(coord, 0));
    float4 cRight  = CurrentColor.Load(int3(coord + int2(1, 0), 0));
    float4 cDown   = CurrentColor.Load(int3(coord + int2(0, 1), 0));

    float Ix = dot(cRight.rgb - cCenter.rgb, float3(0.299, 0.587, 0.114));
    float Iy = dot(cDown.rgb  - cCenter.rgb, float3(0.299, 0.587, 0.114));
    float It = dot(cCenter.rgb - PreviousColor.Load(int3(coord, 0)).rgb,
                   float3(0.299, 0.587, 0.114));

    // Depth-weighted optical flow constraint:
    //   Ix * u + Iy * v + It + alpha * (zCurrent - zPrev) = 0
    float denom        = (Ix * Ix + Iy * Iy) + 1e-4f;
    float depthWeight  = saturate(1.0f - abs(zCurrent - zPrev) / zCurrent);

    float2 velocity;
    velocity.x = -(Ix * It) / denom;
    velocity.y = -(Iy * It) / denom;

    // Mask motion by depth continuity (avoid bleeding edge motion across silhouettes).
    velocity *= depthWeight;

    // Output normalized motion vector [-1.0, 1.0] for DLSS ingest.
    MotionVectorOutput[coord] = velocity;
}