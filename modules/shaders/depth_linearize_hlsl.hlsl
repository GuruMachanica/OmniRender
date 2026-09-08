// filepath: modules/shaders/depth_linearize_hlsl.hlsl
// Depth linearization placeholder for the v0.3.0-alpha pipeline.
//
// The bounded pipeline has the wiring for a real depth-flatten
// pass (so downstream motion + reactive passes can consume a
// linearised depth), but no game-side depth source is plumbed in
// yet for v0.7.0-alpha. Until that wiring lands this shader runs
// as a passthrough so the rest of the pipeline can dispatch and
// bind/unbind without crashing. When the D3D9/DXGI depth path is
// extended in a follow-up release, this file is the single point
// of change: replace the body of CSMain with the linearisation
// formula (e.g. perspective Z reconstruction from the depth SRV
// with the inverse view*proj) and the existing C++ dispatch will
// keep working unchanged.
//
// Thread group size: 16x16x1
// Bindings (must stay in sync with modules/daemon/processing.cpp):
//   t0: InputTexture  (RGBA8 unorm in v0.7.0-alpha placeholder mode)
//   u0: OutputTexture (RGBA8 unorm, write target)
//   b0: ProcessingConstants (SourceWidth/Height, TargetWidth/Height,
//                             JitterX/Y, BlendFactor, Pad1)

Texture2D<float4>    InputTexture  : register(t0);
RWTexture2D<float4>  OutputTexture : register(u0);

cbuffer ProcessingConstants : register(b0) {
    float SourceWidth;
    float SourceHeight;
    float TargetWidth;
    float TargetHeight;
    float JitterX;
    float JitterY;
    float BlendFactor;
    float Pad1;
};

[numthreads(16, 16, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID) {
    uint width, height;
    OutputTexture.GetDimensions(width, height);

    if (dtid.x >= width || dtid.y >= height) {
        return;
    }

    // Placeholder: write the input straight through. The full
    // linearisation pass will overwrite this body when the depth
    // source binding lands.
    float4 src = InputTexture.Load(int3(dtid.xy, 0));
    OutputTexture[dtid.xy] = src;
}
