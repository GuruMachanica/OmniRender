// filepath: modules/shaders/auto_hdr_tonemap.hlsl
// Bounded HDR / inverse tone mapping fallback for the v0.3.0-alpha pipeline.
//
// This is the first non-neural tone map pass. It runs on the daemon's
// bounded work texture and does not allocate a separate full-resolution
// buffer, so VRAM usage stays controlled even when the source frame is
// large. When TensorRT is available later, this path can be replaced by an
// engine dispatch; until then it is a lightweight compute fallback.
//
// The tone-map constants are baked into the shader for v0.7.0-alpha
// because the C++ side does not yet bind a b0 constant buffer for the
// HDR path. Once the b0 binding lands, swap the baked literals for
// the cbuffer reads below.
//
// Thread group size: 16x16x1
// Bindings:
//   t0: SourceRGBA (input frame)
//   u0: TargetRGBA UAV (in-place when possible)

Texture2D<float4>  Source : register(t0);
RWTexture2D<float4> Target : register(u0);

static const float kExposure   = 1.0f;
static const float kKey        = 0.18f;
static const float kSaturation = 1.0f;

float3 TonemapAGX(float3 color, float exposure, float key, float saturation) {
    float3 x = color * exposure;
    float luminance = dot(x, float3(0.2126729f, 0.7151522f, 0.0721750f));
    float tone = 1.0f / (1.0f + luminance);
    float3 mapped = saturate(x * tone);
    float gray = dot(mapped, float3(0.2126729f, 0.7151522f, 0.0721750f));
    return saturate(lerp(float3(gray, gray, gray), mapped, saturation));
}

[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID) {
    int2 coord = dispatchThreadId.xy;
    float4 src = Source.Load(int3(coord, 0));
    float3 mapped = TonemapAGX(src.rgb, kExposure, kKey, kSaturation);
    Target[coord] = float4(mapped, src.a);
}
