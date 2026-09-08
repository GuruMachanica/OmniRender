# filepath: backends/CMakeLists.txt
set(BACKENDS_SOURCES
    reconstruction/IReconstructionBackend.h
)

if(WIN32)
    list(APPEND BACKENDS_SOURCES
        reconstruction/dlss/DlssLoader.h
        reconstruction/dlss/DlssLoader.cpp
        reconstruction/dlss/DlssReconstructionBackend.h
        reconstruction/dlss/DlssReconstructionBackend.cpp
    )
endif()

add_library(OmniRenderBackends STATIC ${BACKENDS_SOURCES})

target_include_directories(OmniRenderBackends PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(OmniRenderBackends PUBLIC
    omnirender::core
    omnirender::graphics
)

if(WIN32)
    target_link_libraries(OmniRenderBackends PRIVATE
        d3d11
        dxgi
    )
endif()

target_compile_features(OmniRenderBackends PUBLIC cxx_std_20)

add_library(omnirender::backends ALIAS OmniRenderBackends)
