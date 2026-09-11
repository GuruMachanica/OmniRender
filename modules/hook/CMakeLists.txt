# filepath: modules/hook/CMakeLists.txt
# Client-side injected proxy DLL.
add_library(omnirender-hook SHARED
    d3d9_interceptor.cpp
    d3d9_post.cpp
    d3d9_hud.cpp
    d3d9_shared_surfaces.cpp
    dxgi_interceptor.cpp
    dxgi_capture_frame.cpp
    dxgi_depth_capture.cpp
    dxgi_proxy.cpp
    depth_locator.cpp
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

install(TARGETS omnirender-hook
    RUNTIME DESTINATION bin)

# Proxy DLL artifacts (audit plan Commit 10). The hook DLL already exports
# the proxy entry points each loader path needs:
#   - Direct3DCreate9            -> drop in as d3d9.dll
#   - CreateDXGIFactory{,1,2}    -> drop in as dxgi.dll
#   - wglSwapBuffers forwarding  -> drop in as opengl32.dll
# Emit one renamed copy per proxy name next to the canonical DLL and install
# all of them so the installer / user can pick the right drop-in. DLLMain
# (main.cpp) detects which proxy role it was loaded as via module_name.cpp
# and only installs the matching hook set.
if(WIN32)
    foreach(proxy_name d3d9 dxgi opengl32)
        add_custom_command(TARGET omnirender-hook POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:omnirender-hook>
                $<TARGET_FILE_DIR:omnirender-hook>/${proxy_name}.dll
            COMMENT "Emitting ${proxy_name}.dll proxy from omnirender-hook.dll")
        install(FILES "$<TARGET_FILE_DIR:omnirender-hook>/${proxy_name}.dll"
            DESTINATION bin/proxies
            OPTIONAL)
    endforeach()
endif()
