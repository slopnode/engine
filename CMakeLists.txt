cmake_minimum_required(VERSION 3.22)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

project(slopengine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(lib)

set(SLOPLIB_SOURCES
    src/core/package.cpp
    src/core/package_meta.cpp
    src/core/vfs.cpp
    src/assets/asset_store.cpp
    src/assets/geo_loader.cpp
    src/assets/material_loader.cpp
    src/assets/skeleton_loader.cpp
    src/assets/anim_loader.cpp
    src/game/app_config.cpp
    src/map/brush.cpp
    src/map/bsp_analyze.cpp
    src/map/bsp_build.cpp
    src/map/bsp_io.cpp
    src/map/bsp_ray.cpp
    src/map/csg_compile.cpp
    src/map/csg_script.cpp
    src/map/lightmap.cpp
    src/map/map_meta.cpp
    src/map/quad_bvh.cpp
    src/map/radiosity.cpp
    src/map/uv_math.cpp
)

add_library(sloplib STATIC ${SLOPLIB_SOURCES})
target_include_directories(sloplib PUBLIC src)
target_link_libraries(sloplib PUBLIC raylib s7)

if(MSVC)
    target_compile_options(sloplib PRIVATE /W4 /permissive-)
else()
    target_compile_options(sloplib PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(slopengine
    src/main.cpp
    src/core/user_paths.cpp
    src/game/user_settings.cpp
    src/game/app.cpp
    src/input/input_module.cpp
    src/camera/camera_module.cpp
    src/interact/interact_module.cpp
    src/ui/ui_module.cpp
    src/physics/physics_world.cpp
    src/physics/physics_module.cpp
    src/physics/map_collision.cpp
    src/render/transform.cpp
    src/render/animation_player.cpp
    src/render/render_module.cpp
)

target_link_libraries(slopengine PRIVATE
    sloplib
    flecs::flecs_static
    Jolt::Jolt
    rlImGui
)

if(MSVC)
    target_compile_options(slopengine PRIVATE /W4 /permissive-)
else()
    target_compile_options(slopengine PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(slopbsp tools/slopbsp/main.cpp)
target_link_libraries(slopbsp PRIVATE sloplib)
if(MSVC)
    target_compile_options(slopbsp PRIVATE /W4 /permissive-)
else()
    target_compile_options(slopbsp PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(sloprad tools/sloprad/main.cpp)
target_link_libraries(sloprad PRIVATE sloplib)
if(MSVC)
    target_compile_options(sloprad PRIVATE /W4 /permissive-)
else()
    target_compile_options(sloprad PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(cmake/Doxygen.cmake)
