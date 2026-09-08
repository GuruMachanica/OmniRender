// filepath: modules/shaders/upscale_hlsl.hlsl
// Bounded Lanczos-style bicubic upscale for the v0.3.0-alpha pipeline.
//
// This is the first real plugin-free upscale pass. It operates on the
// daemon's clamped working resolution and writes into the work UAV, so
// it never allocates a full-resolution copy of the game's backbuffer.
//
// Thread group size: 16x16x1
// Bindings:
//   t0: source RGBA (DXGI_FORMAT_R8G8B8A8_UNORM or similar)
//   u0: target RGBA UAV
//   b0: UpscaleConstants

SamplerState PointSampler : register(s0);
SamplerState LinearSampler : register(s1);

Texture2D<float4> Source : register(t0);
RWTexture2D<float4> Target : register(u0);

cbuffer UpscaleConstants : register(b0) {
    float SourceWidth;
    float SourceHeight;
    float TargetWidth;
    float TargetHeight;
};

// Simple Lanczos-3 style kernel weights.
float Lanczos3(float x) {
    float ax = abs(x);
    if (ax < 0.0001f) return 1.0f;
    float pi_x = 3.1415926535f * ax;
    float s = sin(pi_x) / (pi_x);
    float s2 = sin(pi_x / 3.0f) / (pi_x / 3.0f);
    return s * s2;
}

float4 SampleBicubic(float2 uv) {
    float2 p = uv * float2(SourceWidth, SourceHeight);
    float2 f = floor(p - 0.5f);
    float2 d = p - (f + 0.5f);

    float wx[4];
    float wy[4];
    for (int i = -1; i <= 2; ++i) {
        wx[i + 1] = Lanczos3(d.x - float(i));
        wy[i + 1] = Lanczos3(d.y - float(i));
    }

    float4 color = float4(0.0f, 0.0f, 0.0f, 0.0f);
    for (int y = -1; y <= 2; ++y) {
        for (int x = -1; x <= 2; ++x) {
            int2 coord = int2(f) + int2(x, y);
            if (coord.x < 0 || coord.y < 0 ||
                coord.x >= SourceWidth || coord.y >= SourceHeight) {
                continue;
            }
            float4 s = Source.Load(int3(coord, 0));
            color += s * wx[x + 1] * wy[y + 1];
        }
    }

    return color;
}

[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID) {
    int2 coord = dispatchThreadId.xy;
    if (coord.x >= TargetWidth || coord.y >= TargetHeight) return;
    float2 uv = float2(coord) / float2(TargetWidth, TargetHeight);
    Target[coord] = SampleBicubic(uv);
}
