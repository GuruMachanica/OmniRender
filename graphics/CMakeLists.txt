# filepath: graphics/CMakeLists.txt
set(GRAPHICS_SOURCES
    abstraction/IGraphicsDevice.h
    abstraction/IGraphicsTexture.h
    abstraction/IGraphicsBuffer.h
    abstraction/ICommandContext.h
    abstraction/GraphicsDevice.cpp
)

if(WIN32)
    list(APPEND GRAPHICS_SOURCES
        d3d11/D3D11TypeConversions.h
        d3d11/D3D11TypeConversions.cpp
        d3d11/D3D11GraphicsTexture.h
        d3d11/D3D11GraphicsTexture.cpp
        d3d11/D3D11GraphicsBuffer.h
        d3d11/D3D11GraphicsBuffer.cpp
        d3d11/D3D11CommandContext.h
        d3d11/D3D11CommandContext.cpp
        d3d11/D3D11GraphicsDevice.h
        d3d11/D3D11GraphicsDevice.cpp
    )
endif()

add_library(OmniRenderGraphics STATIC ${GRAPHICS_SOURCES})

target_include_directories(OmniRenderGraphics PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(OmniRenderGraphics PUBLIC
    omnirender::core
)

if(WIN32)
    target_link_libraries(OmniRenderGraphics PRIVATE
        d3d11
        dxgi
    )
endif()

target_compile_features(OmniRenderGraphics PUBLIC cxx_std_20)

add_library(omnirender::graphics ALIAS OmniRenderGraphics)
