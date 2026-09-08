# filepath: modules/common/CMakeLists.txt
# Common library shared by the hook and the daemon.
# Header-only for the IPC / logging / config pieces; the vtable-hook
# helper compiles into a small static library so both targets can link
# against it.

add_library(omnirender_common STATIC
    config.cpp
    vtable_hook.cpp)

add_library(omnirender::common ALIAS omnirender_common)

target_include_directories(omnirender_common
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_features(omnirender_common PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(omnirender_common PUBLIC /W4)
endif()
