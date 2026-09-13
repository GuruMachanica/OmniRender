# filepath: CMakeLists.txt
# Root build for OmniRender. Pulls in each module via add_subdirectory().
# Per-module details (sources, libraries, options) live inside the
# modules/<name>/CMakeLists.txt of that module.
cmake_minimum_required(VERSION 3.20)
project(OmniRender
    VERSION 0.7.0
    DESCRIPTION "Out-of-process frame capture and bounded reconstruction for legacy 3D games"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus /std:c++20)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

include(GNUInstallDirs)

if(WIN32)
    set(OMNIRENDER_APP_ICON "${CMAKE_SOURCE_DIR}/assets/omnirender.rc")
    set_source_files_properties("${OMNIRENDER_APP_ICON}"
        PROPERTIES COMPILE_FLAGS "/I \"${CMAKE_SOURCE_DIR}/assets\"")

    option(OMNIRENDER_BUILD_HOOK       "Build the x86/x64 hook proxy DLL"     ON)
    option(OMNIRENDER_BUILD_DAEMON     "Build the 64-bit OmniRender daemon"    ON)
    option(OMNIRENDER_BUILD_UI         "Build the OmniRender Control Center UI" ON)
else()
    option(OMNIRENDER_BUILD_HOOK       "Build the x86/x64 hook proxy DLL"     OFF)
    option(OMNIRENDER_BUILD_DAEMON     "Build the 64-bit OmniRender daemon"    OFF)
    option(OMNIRENDER_BUILD_UI         "Build the OmniRender Control Center UI" OFF)
endif()
option(OMNIRENDER_BUILD_TESTS      "Build unit tests"                       OFF)
option(OMNIRENDER_BUILD_TEST_HOST  "Build the in-process test renderer"    OFF)
option(OMNIRENDER_USE_NGX          "Link against NVIDIA Streamline / NGX"   OFF)
option(OMNIRENDER_USE_TRT          "Link against NVIDIA TensorRT + CUDA"   OFF)

# Platform-independent core, graphics abstraction, backends, and runtime
add_subdirectory(core)
add_subdirectory(graphics)
add_subdirectory(backends)
add_subdirectory(runtime)

# Existing modules
add_subdirectory(modules/common)

if(OMNIRENDER_BUILD_HOOK)
    add_subdirectory(modules/hook)
endif()

if(OMNIRENDER_BUILD_DAEMON)
    add_subdirectory(modules/daemon)
endif()

if(OMNIRENDER_BUILD_UI)
    add_subdirectory(modules/ui)
endif()

# Real-game launcher: drops the hook proxies next to the game exe, backs up
# originals, starts the daemon, then resumes the game. Windows-only tool.
if(WIN32 AND (OMNIRENDER_BUILD_HOOK OR OMNIRENDER_BUILD_DAEMON))
    add_executable(OmniRenderLaunch tools/launcher/OmniRenderLaunch.cpp)
    target_compile_definitions(OmniRenderLaunch PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(OmniRenderLaunch PRIVATE /W4)
    endif()
    install(TARGETS OmniRenderLaunch RUNTIME DESTINATION bin)
endif()

if(OMNIRENDER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Keep the build tree honest: list the v0.3.0-alpha source files so
# future contributors do not rediscover them by accident.
#
# modules/daemon/processing.cpp, processing.h
# modules/daemon/pipeline.cpp, pipeline.h
# modules/daemon/test_host.cpp
# modules/daemon/presentation_win.h, interop_d3d11.h, ipc_server.h,
#   processing.h, pipeline.h, tonemap_neural.h
# modules/hook/depth_format.cpp
# modules/common/pipeline_config.h
# modules/common/log_file.h
# modules/shaders/upscale_hlsl.hlsl
# modules/shaders/reconstruct_hlsl.hlsl
# modules/shaders/auto_hdr_tonemap.hlsl
# modules/shaders/passthrough_ps.hlsl
# docs/pipeline_v0.3.md, docs/roadmap_v0.3.md, docs/3rd_party_deps.md,
#   docs/AUDIT_TODO.md

# Install layout (matches PRD section 9 - binary distribution)
if(OMNIRENDER_BUILD_DAEMON)
    install(TARGETS OmniRenderDaemon
        RUNTIME DESTINATION bin
        ARCHIVE DESTINATION lib)
endif()
install(DIRECTORY modules/shaders DESTINATION shaders)

# Ship the hook proxy DLLs next to the launcher so OmniRenderLaunch.exe can
# deploy them into game directories without a manual copy step.
if(OMNIRENDER_BUILD_HOOK)
    install(FILES
        $<TARGET_FILE_DIR:omnirender-hook>/d3d9.dll
        $<TARGET_FILE_DIR:omnirender-hook>/dxgi.dll
        $<TARGET_FILE_DIR:omnirender-hook>/opengl32.dll
        DESTINATION bin
        OPTIONAL)
endif()

# Install extended pipeline docs so they ship with the source tree.
install(FILES docs/pipeline_v0.3.md docs/roadmap_v0.3.md docs/3rd_party_deps.md
    DESTINATION doc)