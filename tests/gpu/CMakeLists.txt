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

# Ship the core temporal CSOs next to the test host so the temporal passes
# load their canonical GPU accelerators (MotionReproject.cso etc.) when DXC
# compiled them. No-op when OMNIRENDER_BUILD_CSO=OFF / DXC missing.
if(TARGET omnirender_daemon_shaders)
    add_dependencies(gpu_test_host omnirender_daemon_shaders)
    add_custom_command(TARGET gpu_test_host POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE_DIR:OmniRenderDaemon>/MotionReproject.cso
            $<TARGET_FILE_DIR:OmniRenderDaemon>/Disocclusion.cso
            $<TARGET_FILE_DIR:OmniRenderDaemon>/ReactiveMask.cso
            $<TARGET_FILE_DIR:OmniRenderDaemon>/DepthLinearize.cso
            $<TARGET_FILE_DIR:gpu_test_host>
        COMMENT "Copying core temporal CSOs next to gpu_test_host")
endif()

add_test(NAME gpu_test_host COMMAND gpu_test_host)
