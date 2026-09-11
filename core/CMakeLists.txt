# filepath: core/CMakeLists.txt
add_library(OmniRenderCore STATIC
    capability/Platform.h
    capability/GraphicsApi.h
    capability/RuntimeCapabilities.h
    resources/TextureFormat.h
    resources/TextureDesc.h
    resources/BufferDesc.h
    resources/ResourceHandle.h
    resources/GpuTexture.h
    resources/GpuTexture.cpp
    resources/GpuBuffer.h
    resources/GpuBuffer.cpp
    frame/Resolution.h
    frame/FrameTiming.h
    frame/FrameValidity.h
    frame/FrameContext.h
    camera/CameraState.h
    temporal/JitterState.h
    temporal/HistoryState.h
    temporal/HistoryManager.h
    temporal/HistoryManager.cpp
    temporal/TemporalPassBase.h
    temporal/ShaderPath.h
    temporal/DepthProvider.h
    temporal/DepthProvider.cpp
    temporal/MotionReprojectionPass.h
    temporal/MotionReprojectionPass.cpp
    temporal/DisocclusionPass.h
    temporal/DisocclusionPass.cpp
    temporal/ReactiveMaskPass.h
    temporal/ReactiveMaskPass.cpp
    graph/RenderGraphTypes.h
    graph/RenderGraph.h
    graph/RenderGraph.cpp
)

target_include_directories(OmniRenderCore PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(OmniRenderCore PUBLIC cxx_std_20)

add_library(omnirender::core ALIAS OmniRenderCore)
