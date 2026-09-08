# filepath: modules/ui/CMakeLists.txt
# OmniRender Control Center UI.

add_executable(OmniRenderControlPanel WIN32
    control_panel.cpp
    control_panel_theme.cpp
    game_detector.cpp
    ../daemon/capability_matrix.cpp
    ${OMNIRENDER_APP_ICON})

target_include_directories(OmniRenderControlPanel PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/modules/common
    ${CMAKE_SOURCE_DIR}/modules/daemon)

target_link_libraries(OmniRenderControlPanel PRIVATE
    omnirender::common
    comctl32
    comdlg32
    shell32
    user32
    gdi32)

if(MSVC)
    target_compile_options(OmniRenderControlPanel PRIVATE /W4)
    set_target_properties(OmniRenderControlPanel PROPERTIES
        WIN32_EXECUTABLE TRUE)
endif()

install(TARGETS OmniRenderControlPanel
    RUNTIME DESTINATION bin)
