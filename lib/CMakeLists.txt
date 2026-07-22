set(FLECS_STATIC ON CACHE BOOL "" FORCE)
set(FLECS_SHARED OFF CACHE BOOL "" FORCE)
set(FLECS_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(flecs)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(OPENGL_VERSION "4.3" CACHE STRING "Force a specific OpenGL Version?" FORCE)
set(SUPPORT_MODULE_RAUDIO OFF CACHE BOOL "" FORCE)

if(UNIX AND NOT APPLE)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(WAYLAND_CLIENT QUIET wayland-client>=0.2.7)
        pkg_check_modules(WAYLAND_CURSOR QUIET wayland-cursor>=0.2.7)
        pkg_check_modules(WAYLAND_EGL QUIET wayland-egl>=0.2.7)
        pkg_check_modules(XKBCOMMON QUIET xkbcommon>=0.5.0)
        find_program(WAYLAND_SCANNER wayland-scanner QUIET)
        if(WAYLAND_CLIENT_FOUND AND WAYLAND_CURSOR_FOUND AND WAYLAND_EGL_FOUND AND XKBCOMMON_FOUND AND WAYLAND_SCANNER)
            message(STATUS "Wayland support: enabled")
            set(GLFW_BUILD_WAYLAND ON CACHE BOOL "" FORCE)
        else()
            message(STATUS "Wayland support: disabled (missing packages)")
            set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
        endif()
    else()
        message(STATUS "Wayland support: disabled (pkg-config not found)")
        set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
    endif()
endif()

add_subdirectory(raylib)
target_compile_definitions(raylib PUBLIC SUPPORT_GPU_SKINNING=1)

include(${CMAKE_SOURCE_DIR}/cmake/SoLoud.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/SteamAudio.cmake)

set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(JPH_USE_DX12 OFF CACHE BOOL "" FORCE)
set(JPH_USE_VK OFF CACHE BOOL "" FORCE)
set(JPH_USE_MTL OFF CACHE BOOL "" FORCE)
set(JPH_USE_CPU_COMPUTE ON CACHE BOOL "" FORCE)
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
add_subdirectory(JoltPhysics/Build)

set(S7_UPSTREAM "${CMAKE_CURRENT_SOURCE_DIR}/s7/s7.c")
set(S7_PATCHED "${CMAKE_CURRENT_BINARY_DIR}/s7_patched.c")
set(S7_PREPARE_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/prepare_s7.cmake")

if(MSVC)
    set(S7_APPLY_MSVC_PATCH ON)
else()
    set(S7_APPLY_MSVC_PATCH OFF)
endif()

add_custom_command(
    OUTPUT "${S7_PATCHED}"
    COMMAND ${CMAKE_COMMAND}
        "-DINPUT=${S7_UPSTREAM}"
        "-DOUTPUT=${S7_PATCHED}"
        "-DAPPLY_MSVC_PATCH=${S7_APPLY_MSVC_PATCH}"
        -P "${S7_PREPARE_SCRIPT}"
    DEPENDS "${S7_UPSTREAM}" "${S7_PREPARE_SCRIPT}"
    VERBATIM
)

add_library(s7 STATIC s7_wrapper.c)
add_custom_target(s7_patched_source DEPENDS "${S7_PATCHED}")
add_dependencies(s7 s7_patched_source)
target_include_directories(s7 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/s7 ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(s7 PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
set_target_properties(s7 PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)

if(MSVC)
    target_compile_definitions(s7 PRIVATE
        MUS_CONFIG_H_LOADED=1
        _CRT_SECURE_NO_WARNINGS
    )
    target_compile_options(s7 PRIVATE /W3)
else()
    target_compile_options(s7 PRIVATE -Wall -Wextra)
endif()

if(MSVC)
    foreach(lib_target flecs_static raylib s7 glfw Jolt)
        if(TARGET ${lib_target})
            set_property(TARGET ${lib_target} PROPERTY
                MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
        endif()
    endforeach()
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(s7 PUBLIC m dl)
endif()

set(IMGUI_SOURCES
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_widgets.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_demo.cpp
)

add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/imgui)
target_compile_definitions(imgui PUBLIC
    IMGUI_DISABLE_OBSOLETE_FUNCTIONS
    IMGUI_DISABLE_OBSOLETE_KEYIO
)

set(RLIMGUI_SOURCES
    rlImGui/rlImGui.cpp
)

add_library(rlImGui STATIC ${RLIMGUI_SOURCES})
target_include_directories(rlImGui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/rlImGui)
target_link_libraries(rlImGui PUBLIC imgui raylib)
