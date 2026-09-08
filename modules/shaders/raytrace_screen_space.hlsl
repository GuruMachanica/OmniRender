// filepath: modules/shaders/raytrace_screen_space.hlsl
// Screen-Space Ray Tracing (SSRT) Compute Shader: Ray-Traced Reflections (SSR) & Ambient Occlusion (RTAO)
// HLSL Compute Shader (cs_5_0) for Direct3D 11.

Texture2D<float4> ColorTexture : register(t0);
Texture2D<float>  DepthTexture : register(t1);
RWTexture2D<float4> OutputTexture : register(u0);

cbuffer RaytraceConstants : register(b0) {
    float4 ViewParams;  // x: Near, y: Far, z: Width, w: Height
    float4 RayParams;   // x: MaxDistance, y: StepSize, z: NumSteps, w: Intensity
    float4x4 InvProj;
    float4x4 Proj;
};

// Reconstruct view-space position from screen UV and normalized depth
float3 ReconstructViewPos(float2 uv, float depth) {
    float4 clip = float4(uv * 2.0f - 1.0f, depth, 1.0f);
    clip.y = -clip.y;
    float4 viewPos = mul(clip, InvProj);
    return viewPos.xyz / max(viewPos.w, 1e-6f);
}

// Project view-space position to screen UV and NDC depth
float3 ProjectToScreen(float3 viewPos) {
    float4 clip = mul(float4(viewPos, 1.0f), Proj);
    if (abs(clip.w) < 1e-6f) return float3(-1.0f, -1.0f, -1.0f);
    float3 ndc = clip.xyz / clip.w;
    ndc.y = -ndc.y;
    return float3(ndc.xy * 0.5f + 0.5f, ndc.z);
}

[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint2 coord = dispatchThreadId.xy;
    uint width = (uint)ViewParams.z;
    uint height = (uint)ViewParams.w;

    if (coord.x >= width || coord.y >= height) {
        return;
    }

    float2 texelSize = 1.0f / float2(width, height);
    float2 uv = (float2(coord) + 0.5f) * texelSize;

    float depth = DepthTexture.Load(int3(coord, 0));
    float4 baseColor = ColorTexture.Load(int3(coord, 0));

    // Skip skybox / background
    if (depth >= 0.9999f || depth <= 0.0001f) {
        OutputTexture[coord] = baseColor;
        return;
    }

    // 1. Reconstruct view position and surface normal via cross product
    float3 P = ReconstructViewPos(uv, depth);

    float d_right = DepthTexture.Load(int3(min(coord + uint2(1, 0), uint2(width - 1, height - 1)), 0));
    float d_down  = DepthTexture.Load(int3(min(coord + uint2(0, 1), uint2(width - 1, height - 1)), 0));
    float3 P_right = ReconstructViewPos(uv + float2(texelSize.x, 0.0f), d_right);
    float3 P_down  = ReconstructViewPos(uv + float2(0.0f, texelSize.y), d_down);

    float3 dPdx = P_right - P;
    float3 dPdy = P_down - P;
    float3 N = normalize(cross(dPdx, dPdy));

    // Ensure normal faces the camera
    if (N.z > 0.0f) N = -N;

    // View direction in view space
    float3 V = normalize(-P);
    float NdotV = saturate(dot(N, V));

    // Reflection vector
    float3 R = reflect(-V, N);

    // 2. Screen-Space Ray Marching
    float maxDist = RayParams.x > 0.0f ? RayParams.x : 50.0f;
    float stepSize = RayParams.y > 0.0f ? RayParams.y : 0.25f;
    int maxSteps = RayParams.z > 0.0f ? (int)RayParams.z : 32;
    float intensity = RayParams.w > 0.0f ? RayParams.w : 0.5f;

    float3 hitColor = float3(0.0f, 0.0f, 0.0f);
    float hitWeight = 0.0f;

    // March along reflection ray
    float t = stepSize * 2.0f;
    for (int step = 0; step < maxSteps; ++step) {
        float3 rayPos = P + R * t;
        float3 screenCoord = ProjectToScreen(rayPos);

        // Check if ray left screen boundaries
        if (screenCoord.x < 0.0f || screenCoord.x > 1.0f ||
            screenCoord.y < 0.0f || screenCoord.y > 1.0f) {
            break;
        }

        uint2 sampleCoord = (uint2)(screenCoord.xy * float2(width, height));
        float sampledDepth = DepthTexture.Load(int3(sampleCoord, 0));
        float3 sampledPos = ReconstructViewPos(screenCoord.xy, sampledDepth);

        // Check depth intersection with thickness threshold
        float depthDiff = rayPos.z - sampledPos.z;
        if (depthDiff > 0.0f && depthDiff < (stepSize * 2.5f)) {
            // Hit detected! Screen edge fade
            float2 edgeDist = min(screenCoord.xy, 1.0f - screenCoord.xy);
            float edgeFade = saturate(min(edgeDist.x, edgeDist.y) * 10.0f);

            // Fresnel term (Schlick's approximation with F0 = 0.04 for dielectric)
            float fresnel = 0.04f + 0.96f * pow(1.0f - NdotV, 5.0f);

            hitColor = ColorTexture.Load(int3(sampleCoord, 0)).rgb;
            hitWeight = edgeFade * fresnel * intensity;
            break;
        }

        t += stepSize * (1.0f + float(step) * 0.05f); // progressive step growth
    }

    // 3. Contact Ambient Occlusion (RTAO approximation)
    float ao = 1.0f;
    float aoRadius = 0.4f;
    float aoSampleDepth = DepthTexture.Load(int3(clamp(coord + int2(2, 2), int2(0, 0), int2(width - 1, height - 1)), 0));
    float3 aoSamplePos = ReconstructViewPos(uv + texelSize * 2.0f, aoSampleDepth);
    float3 aoDiff = aoSamplePos - P;
    if (length(aoDiff) < aoRadius && dot(normalize(aoDiff), N) > 0.1f) {
        ao = 0.85f;
    }

    // Composite: Color * AO + Reflection
    float3 finalColor = (baseColor.rgb * ao) + (hitColor * hitWeight);
    OutputTexture[coord] = float4(finalColor, baseColor.a);
}
