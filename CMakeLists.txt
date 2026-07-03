cmake_minimum_required(VERSION 3.22)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

project(daggerlike LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(lib)

add_executable(daggerlike
    src/main.cpp
    src/core/package.cpp
    src/core/vfs.cpp
    src/assets/asset_store.cpp
    src/assets/geo_loader.cpp
    src/assets/material_loader.cpp
    src/assets/skeleton_loader.cpp
    src/assets/anim_loader.cpp
    src/game/app_config.cpp
    src/game/app.cpp
    src/render/transform.cpp
    src/render/animation_player.cpp
    src/render/render_module.cpp
)

target_include_directories(daggerlike PRIVATE src)

target_link_libraries(daggerlike PRIVATE
    flecs::flecs_static
    raylib
    s7
)

if(MSVC)
    target_compile_options(daggerlike PRIVATE /W4 /permissive-)
else()
    target_compile_options(daggerlike PRIVATE -Wall -Wextra -Wpedantic)
endif()
