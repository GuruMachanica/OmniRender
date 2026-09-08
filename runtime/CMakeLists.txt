# filepath: runtime/CMakeLists.txt
add_library(OmniRenderRuntime STATIC
    BackendResolver.h
    BackendResolver.cpp
    Pipeline.h
    Pipeline.cpp
    ../backends/reconstruction/IReconstructionBackend.h
)

target_include_directories(OmniRenderRuntime PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(OmniRenderRuntime PUBLIC
    omnirender::core
    omnirender::graphics
    omnirender::backends
)

target_compile_features(OmniRenderRuntime PUBLIC cxx_std_20)

add_library(omnirender::runtime ALIAS OmniRenderRuntime)
