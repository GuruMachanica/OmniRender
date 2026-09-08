# filepath: modules/hook/CMakeLists.txt
# Client-side injected proxy DLL.
add_library(omnirender-hook SHARED
    d3d9_interceptor.cpp
    d3d9_post.cpp
    d3d9_hud.cpp
    d3d9_shared_surfaces.cpp
    dxgi_interceptor.cpp
    depth_locator.cpp
    depth_format.cpp
    ipc_client.cpp
    opengl_interceptor.cpp
    opengl_interop.cpp
    module_name.cpp
    main.cpp)

target_compile_definitions(omnirender-hook PRIVATE OMNIRENDER_HOOK_EXPORTS)

target_link_libraries(omnirender-hook
    PRIVATE
        omnirender::common)

# OpenGL 1.5+ headers (Windows SDK ships gl.h). No external lib
# needed for the OpenGL 1.5 fixed-function subset we use.
if(MSVC)
    target_compile_options(omnirender-hook PRIVATE /W4)
    target_link_options(omnirender-hook PRIVATE /OPT:NOREF /OPT:NOICF)
    target_link_libraries(omnirender-hook PRIVATE
        d3d9 d3d11 dxgi dxguid opengl32)
endif()

set_target_properties(omnirender-hook PROPERTIES
    OUTPUT_NAME "omnirender-hook"
    PREFIX "")

# When the install target is built, also emit a copy renamed to
# opengl32.dll so users can drop it into an OpenGL game folder to
# capture wglSwapBuffers. The d3d9.dll rename pattern is handled
# the same way: install the DLL under each "proxy name" so the
# installer can pick the right one.
install(TARGETS omnirender-hook
    RUNTIME DESTINATION bin)

# Build an opengl32.dll proxy by post-build renaming. Only emitted
# on Windows because the OpenGL proxy is Windows-only.
if(WIN32)
    add_custom_command(TARGET omnirender-hook POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:omnirender-hook>
            $<TARGET_FILE:omnirender-hook>.opengl32.dll
        COMMENT "Building opengl32.dll proxy from omnirender-hook.dll")
endif()
