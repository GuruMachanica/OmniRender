// Designed to synthesize motion vectors for D3D9/D3D11 games without
// engine-side motion-vector exports, provided OmniRender can acquire
// valid depth and camera matrices.
//
// Output is RG16F in NDC space ([-1, +1]). The DLSS / FSR / XeSS
// backends consume this directly.
//
// Thread group size: 16x16x1
// Bindings:
//   t0: LinearizedDepth   (R32F or R16F)
//   t1: PreviousDepth      (R32F or R16F; optional, for depth-difference validation)
//   u0: MotionOutput       (RG16F)
//   b0: ReprojectConstants (matrices + depth params)

Texture2D<float>  LinearizedDepth  : register(t0);
Texture2D<float>  PreviousDepth    : register(t1);
RWTexture2D<float2> MotionOutput    : register(u0);

cbuffer ReprojectConstants : register(b0) {
    float4x4 ViewProjCurrent;
    float4x4 ViewProjPrevious;
    float4x4 InvViewProjCurrent;
    float2   TexelSize;       // 1.0 / Width, 1.0 / Height
    float    CameraNear;
    float    CameraFar;
    uint     ReversedZ;       // 0 or 1
    uint     MotionValid;     // 1 if camera matrices and inverse are valid
    uint     _Pad0;
    uint     _Pad1;
};

[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID) {
    int2 coord = dispatchThreadId.xy;

    if (MotionValid == 0u) {
        MotionOutput[coord] = float2(0.0f, 0.0f);
        return;
    }

    float depth = LinearizedDepth.Load(int3(coord, 0));
    if (depth <= 0.0f) {
        MotionOutput[coord] = float2(0.0f, 0.0f);
        return;
    }

    float2 ndc = float2(coord) * TexelSize * 2.0f - 1.0f;
    ndc.y = -ndc.y;

    float4 clipCur = float4(ndc, depth, 1.0f);
    float4 world = mul(clipCur, InvViewProjCurrent);
    if (abs(world.w) > 1e-6f) {
        world /= world.w;
    }

    float4 prevClip = mul(world, ViewProjPrevious);
    if (prevClip.w <= 0.0f) {
        MotionOutput[coord] = float2(0.0f, 0.0f);
        return;
    }
    float2 prevNDC = prevClip.xy / prevClip.w;

    float2 motion = prevNDC - ndc;
    motion.y = -motion.y;
    MotionOutput[coord] = motion;
}
