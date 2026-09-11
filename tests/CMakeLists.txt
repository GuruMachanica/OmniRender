# filepath: tests/CMakeLists.txt
# Unit and integration tests for OmniRender.
#
# Run with: ctest --test-dir build -C Release --output-on-failure
# Or directly: build\tests\Release\test_<name>.exe

include(CTest)
enable_testing()

# -------------------------------------------------------------------------
# Platform-Neutral Tests (Runs on Linux, macOS, and Windows)
# -------------------------------------------------------------------------
add_executable(test_core_platform_neutral test_core_platform_neutral.cpp)
target_link_libraries(test_core_platform_neutral PRIVATE
    omnirender::core
    omnirender::runtime
)
add_test(NAME core_platform_neutral COMMAND test_core_platform_neutral)

# -------------------------------------------------------------------------
# Windows & Graphics Platform Tests
# -------------------------------------------------------------------------
if(WIN32)
    add_subdirectory(gpu)

    add_executable(test_core_d3d11_execution test_core_d3d11_execution.cpp)
    target_link_libraries(test_core_d3d11_execution PRIVATE
        omnirender::core
        omnirender::graphics
        omnirender::backends
        omnirender::runtime
        d3d11
        dxgi
    )
    add_test(NAME core_d3d11_execution COMMAND test_core_d3d11_execution)

    # Ship the core temporal CSOs next to the D3D11 execution test so the
    # runtime pipeline's temporal passes exercise the GPU path when DXC is
    # available. No-op when the shader compile target does not exist.
    if(TARGET omnirender_daemon_shaders)
        add_dependencies(test_core_d3d11_execution omnirender_daemon_shaders)
        add_custom_command(TARGET test_core_d3d11_execution POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_BINARY_DIR}/modules/daemon/shaders/MotionReproject.cso
                ${CMAKE_BINARY_DIR}/modules/daemon/shaders/Disocclusion.cso
                ${CMAKE_BINARY_DIR}/modules/daemon/shaders/ReactiveMask.cso
                ${CMAKE_BINARY_DIR}/modules/daemon/shaders/DepthLinearize.cso
                $<TARGET_FILE_DIR:test_core_d3d11_execution>
            COMMENT "Copying core temporal CSOs next to test_core_d3d11_execution")
    endif()

    add_executable(test_ring_buffer test_ring_buffer.cpp)
    target_link_libraries(test_ring_buffer PRIVATE omnirender::common)

    add_executable(test_cso_compile test_cso_compile.cpp)

    add_executable(test_halton test_halton.cpp)
    target_link_libraries(test_halton PRIVATE omnirender::common)

    add_executable(test_config test_config.cpp)
    target_link_libraries(test_config PRIVATE omnirender::common)

    add_executable(test_capability_matrix
        test_capability_matrix.cpp
        ../modules/daemon/capability_matrix.cpp)
    target_link_libraries(test_capability_matrix PRIVATE
        omnirender::common
        opengl32)

    add_executable(test_opengl_capture
        test_opengl_capture.cpp
        ../modules/hook/opengl_interop.cpp)
    target_link_libraries(test_opengl_capture PRIVATE
        omnirender::common
        opengl32 d3d11 dxgi)

    add_executable(test_backend_capabilities
        test_backend_capabilities.cpp
        ../modules/daemon/upscalers/dlss_adapter.cpp
        ../modules/daemon/upscalers/xess_adapter.cpp)
    target_link_libraries(test_backend_capabilities PRIVATE omnirender::common d3d11 dxgi)

    add_executable(test_dlss_gpu
        test_dlss_gpu.cpp
        ../modules/daemon/upscalers/dlss_adapter.cpp)
    target_link_libraries(test_dlss_gpu PRIVATE omnirender::common d3d11 dxgi)

    if(MSVC)
        target_compile_options(test_core_platform_neutral PRIVATE /W4)
        target_compile_options(test_core_d3d11_execution  PRIVATE /W4)
        target_compile_options(test_ring_buffer           PRIVATE /W4)
        target_compile_options(test_cso_compile            PRIVATE /W4)
        target_compile_options(test_halton                 PRIVATE /W4)
        target_compile_options(test_config                 PRIVATE /W4)
        target_compile_options(test_capability_matrix      PRIVATE /W4)
        target_compile_options(test_opengl_capture         PRIVATE /W4)
        target_compile_options(test_backend_capabilities   PRIVATE /W4)
        target_compile_options(test_dlss_gpu               PRIVATE /W4)
    endif()

    option(OMNIRENDER_BUILD_SOAK "Build the OmniRenderSoak harness" ON)
    if(OMNIRENDER_BUILD_SOAK)
        add_executable(OmniRenderSoak soak/soak_main.cpp)
        target_link_libraries(OmniRenderSoak PRIVATE omnirender::common)
        if(MSVC)
            target_compile_options(OmniRenderSoak PRIVATE /W4)
            target_link_libraries(OmniRenderSoak PRIVATE psapi user32)
        endif()
    endif()

    add_test(NAME ring_buffer          COMMAND test_ring_buffer)
    add_test(NAME cso_compile           COMMAND test_cso_compile)
    add_test(NAME halton                COMMAND test_halton)
    add_test(NAME config                COMMAND test_config)
    add_test(NAME capability_matrix     COMMAND test_capability_matrix)
    add_test(NAME opengl_capture        COMMAND test_opengl_capture)
    add_test(NAME backend_capabilities  COMMAND test_backend_capabilities)
    add_test(NAME dlss_gpu              COMMAND test_dlss_gpu)
endif()
