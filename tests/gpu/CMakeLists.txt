# filepath: tests/gpu/CMakeLists.txt
# OmniRender Standalone GPU Integration Test Suite

add_executable(gpu_test_host
    gpu_test_host.cpp
    d3d11/D3D11DeviceFixture.cpp
    fixtures/TestSceneFixtures.cpp
    readback/GpuReadback.cpp
    validation/DebugLogger.cpp
    validation/DiagnosticsReporter.cpp
    validation/ImageMetrics.cpp
    ${OMNIRENDER_APP_ICON}
)

target_link_libraries(gpu_test_host PRIVATE
    omnirender::core
    omnirender::graphics
    omnirender::backends
    omnirender::runtime
    d3d11
    dxgi
)

if(MSVC)
    target_compile_options(gpu_test_host PRIVATE /W4)
endif()

add_test(NAME gpu_test_host COMMAND gpu_test_host)
