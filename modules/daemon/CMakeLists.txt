# filepath: modules/daemon/CMakeLists.txt
# 64-bit processing host.

add_executable(OmniRenderDaemon
    main.cpp
    capability_matrix.cpp
    frame_profiler.cpp
    hw_negotiator.cpp
    ipc_server.cpp
    interop_d3d11.cpp
    optical_flow.cpp
    tonemap_neural.cpp
    pipeline.cpp
    processing.cpp
    history_manager.cpp
    shader_loader.cpp
    passes/motion_pass.cpp
    passes/disocclusion_pass.cpp
    passes/raytrace_pass.cpp
    passes/tonemap_pass.cpp
    hud_overlay.cpp
    presentation_win.cpp
    upscalers/dlss_adapter.cpp
    upscalers/dlss_pipeline.cpp
    upscalers/xess_adapter.cpp
    upscalers/xess_pipeline.cpp
    upscalers/spatial_fallback.cpp
    ${OMNIRENDER_APP_ICON})

target_include_directories(OmniRenderDaemon PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/modules/shaders)

target_link_libraries(OmniRenderDaemon
    PRIVATE
        omnirender::common)

if(MSVC)
    target_compile_options(OmniRenderDaemon PRIVATE /W4)
    target_link_libraries(OmniRenderDaemon PRIVATE
        d3d11 d3dcompiler dxgi dxguid
        user32 gdi32)
    # wWinMain requires the WINDOWS subsystem, not CONSOLE. The
    # default subsystem for executables in MSVC is CONSOLE, which
    # makes the linker look for `main` instead of wWinMain.
    set_target_properties(OmniRenderDaemon PROPERTIES
        WIN32_EXECUTABLE TRUE)
endif()

# Compile the bounded-pipeline HLSL shaders to CSO at build time.
#
# All canonical shaders live in modules/shaders/ and are copied
# into the build tree before DXC is invoked. The CSO files are placed
# under ${CMAKE_CURRENT_BINARY_DIR}/shaders/ and shipped alongside the
# daemon binary in the install step.
#
# Set OMNIRENDER_BUILD_CSO=OFF to skip the shader compile (e.g. when DXC
# is not installed). The daemon will still build and run; the bounded
# pipeline simply stays in passthrough mode.
option(OMNIRENDER_BUILD_CSO "Compile HLSL shaders to CSO at build time" ON)

if(OMNIRENDER_BUILD_CSO)
    find_program(DXC_EXECUTABLE dxc HINTS "$ENV{DXSDK_DIR}/bin" "$ENV{DXIL_DIR}")
    if(NOT DXC_EXECUTABLE)
        find_program(DXC_EXECUTABLE fxc HINTS "$ENV{DXSDK_DIR}/bin")
    endif()
    if(DXC_EXECUTABLE)
        set(DAEMON_SHADER_HLSL
            "${CMAKE_SOURCE_DIR}/modules/shaders/passthrough_ps.hlsl"
            "${CMAKE_SOURCE_DIR}/modules/shaders/upscale_hlsl.hlsl"
            "${CMAKE_SOURCE_DIR}/modules/shaders/reconstruct_hlsl.hlsl"
            "${CMAKE_SOURCE_DIR}/modules/shaders/depth_linearize_hlsl.hlsl"
            "${CMAKE_SOURCE_DIR}/modules/shaders/auto_hdr_tonemap.hlsl"
            "${CMAKE_SOURCE_DIR}/modules/shaders/motion_reproject_hlsl.hlsl"
            "${CMAKE_SOURCE_DIR}/modules/shaders/reactive_mask_hlsl.hlsl"
            "${CMAKE_SOURCE_DIR}/modules/shaders/disocclusion_hlsl.hlsl"
            "${CMAKE_SOURCE_DIR}/modules/shaders/fsr_easu.hlsl"
            "${CMAKE_SOURCE_DIR}/modules/shaders/fsr_rcas.hlsl"
            "${CMAKE_SOURCE_DIR}/modules/shaders/raytrace_screen_space.hlsl")
        set(DAEMON_SHADER_CSOS "")
        foreach(SRC ${DAEMON_SHADER_HLSL})
            get_filename_component(BASENAME ${SRC} NAME_WE)
            set(CSO "${CMAKE_CURRENT_BINARY_DIR}/shaders/${BASENAME}.cso")
            # passthrough_ps.hlsl exports VsMain/PsMain, the others export
            # CSMain. Use a separate DXC invocation for each entry point.
            if(BASENAME STREQUAL "passthrough_ps")
                set(ENTRY "PsMain")
                set(PROFILE "ps_5_0")
                # Also compile the vertex shader entry point
                set(VS_CSO "${CMAKE_CURRENT_BINARY_DIR}/shaders/passthrough_vs.cso")
                add_custom_command(
                    OUTPUT "${VS_CSO}"
                    COMMAND ${DXC_EXECUTABLE}
                        -T vs_5_0
                        -E VsMain
                        -O3 -Qstrip_debug
                        ${SRC}
                        -Fo "${VS_CSO}"
                    DEPENDS ${SRC}
                    COMMENT "Compiling ${BASENAME}.hlsl (vs_5_0::VsMain) -> passthrough_vs.cso"
                    VERBATIM)
                list(APPEND DAEMON_SHADER_CSOS ${VS_CSO})
            else()
                set(ENTRY "CSMain")
                set(PROFILE "cs_5_0")
            endif()
            add_custom_command(
                OUTPUT "${CSO}"
                COMMAND ${DXC_EXECUTABLE}
                    -T ${PROFILE}
                    -E ${ENTRY}
                    -O3 -Qstrip_debug
                    ${SRC}
                    -Fo "${CSO}"
                DEPENDS ${SRC}
                COMMENT "Compiling ${BASENAME}.hlsl (${PROFILE}::${ENTRY}) -> ${BASENAME}.cso"
                VERBATIM)
            list(APPEND DAEMON_SHADER_CSOS ${CSO})
        endforeach()
        add_custom_target(omnirender_daemon_shaders ALL
            DEPENDS ${DAEMON_SHADER_CSOS})
        add_dependencies(OmniRenderDaemon omnirender_daemon_shaders)
        # Also copy CSOs to a stable location next to the binary so the
        # daemon can load them at runtime without depending on the
        # build tree layout.
        add_custom_command(TARGET OmniRenderDaemon POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${DAEMON_SHADER_CSOS}
                $<TARGET_FILE_DIR:OmniRenderDaemon>)
        install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shaders/"
            DESTINATION shaders
            OPTIONAL)
        message(STATUS "OmniRender: HLSL -> CSO compile enabled via ${DXC_EXECUTABLE}")
    else()
        message(WARNING "OmniRender: DXC not found; skipping HLSL -> CSO compile. "
            "Set DXC_EXECUTABLE or install dxc on PATH to enable the bounded pipeline.")
    endif()
endif()

# Optional vendor SDKs.
if(OMNIRENDER_USE_NGX)
    target_compile_definitions(OmniRenderDaemon PRIVATE OMNIRENDER_USE_NGX=1)
    target_link_libraries(OmniRenderDaemon PRIVATE sl.interposer sl.d3d11 ngx_d3d11)
endif()

if(OMNIRENDER_USE_TRT)
    target_compile_definitions(OmniRenderDaemon PRIVATE OMNIRENDER_USE_TRT=1)
    target_link_libraries(OmniRenderDaemon PRIVATE nvinfer nvonnxparser cudart)
endif()

# Test host (Package 7 / QA-01). A small D3D11 producer that
# renders a rotating cube into a shareable color + depth surface
# and pushes FrameSlots to the IPC ring. Used as the producer
# side of the smoke test wired into CI. ON by default so every
# build verifies the IPC contract end-to-end.
option(OMNIRENDER_BUILD_TEST_HOST "Build the OmniRenderTestHost in-process test renderer" ON)
if(OMNIRENDER_BUILD_TEST_HOST)
    add_executable(OmniRenderTestHost
        test_host.cpp
        test_host_renderer.cpp
        ${OMNIRENDER_APP_ICON})
    target_include_directories(OmniRenderTestHost PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/modules/shaders)
    target_link_libraries(OmniRenderTestHost
        PRIVATE
            omnirender::common)
    if(MSVC)
        target_compile_options(OmniRenderTestHost PRIVATE /W4)
        target_link_libraries(OmniRenderTestHost PRIVATE
            d3d11 d3dcompiler dxgi dxguid
            user32 gdi32)
    endif()
endif()
