cmake_minimum_required(VERSION 3.22)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

project(slopengine LANGUAGES C CXX)

option(SLOPENGINE_DOCS_ONLY "Configure only the Doxygen docs target" OFF)

if(SLOPENGINE_DOCS_ONLY)
    set(SLOPENGINE_BUILD_DOCS ON CACHE BOOL "" FORCE)
    include(cmake/Doxygen.cmake)
    return()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)

set(SLOPENGINE_ENGINE_PACKAGE_DIR "${CMAKE_SOURCE_DIR}/packages/engine"
    CACHE PATH "Fallback directory for the always-mounted engine package")

file(TO_CMAKE_PATH "${SLOPENGINE_ENGINE_PACKAGE_DIR}" _slop_engine_pkg_dir)
string(REPLACE "\\" "\\\\" _slop_engine_pkg_dir_escaped "${_slop_engine_pkg_dir}")
string(REPLACE "\"" "\\\"" _slop_engine_pkg_dir_escaped "${_slop_engine_pkg_dir_escaped}")
set(SLOPENGINE_ENGINE_PACKAGE_DIR_ESCAPED "${_slop_engine_pkg_dir_escaped}")

configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/engine_package_dir.hpp.in"
    "${CMAKE_BINARY_DIR}/generated/core/engine_package_dir.hpp"
    @ONLY
)

add_subdirectory(lib)

set(SLOPLIB_SOURCES
    src/core/engine_package.cpp
    src/core/package.cpp
    src/core/package_meta.cpp
    src/core/vfs.cpp
    src/assets/asset_store.cpp
    src/assets/geo_loader.cpp
    src/assets/icon_atlas.cpp
    src/assets/material_loader.cpp
    src/assets/saudio_loader.cpp
    src/assets/sprite_loader.cpp
    src/assets/sprite_anim_loader.cpp
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
    src/map/csg_write.cpp
    src/map/things_script.cpp
    src/map/things_write.cpp
    src/map/graph_script.cpp
    src/map/lightmap.cpp
    src/map/light_sample.cpp
    src/map/map_meta.cpp
    src/map/prefab.cpp
    src/map/quad_bvh.cpp
    src/map/radiosity.cpp
    src/map/radiosity_gpu.cpp
    src/map/radiosity_lights.cpp
    src/map/uv_math.cpp
    src/render/sprite_billboard.cpp
)

add_library(sloplib STATIC ${SLOPLIB_SOURCES})
target_include_directories(sloplib PUBLIC
    src
    "${CMAKE_BINARY_DIR}/generated"
)
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
    src/input/action_registry.cpp
    src/input/bind_code.cpp
    src/input/input_module.cpp
    src/camera/camera_module.cpp
    src/interact/interact_module.cpp
    src/ui/ui_module.cpp
    src/ui/imgui_fonts.cpp
    src/physics/physics_world.cpp
    src/physics/physics_module.cpp
    src/physics/map_collision.cpp
    src/audio/audio_world.cpp
    src/audio/audio_module.cpp
    src/map/things_spawn.cpp
    src/render/transform.cpp
    src/render/animation_player.cpp
    src/render/sprite_animator.cpp
    src/render/dynamic_light.cpp
    src/render/dynamic_light_shadows.cpp
    src/render/render_module.cpp
    src/render/hud.cpp
    src/script/scheme_call.cpp
    src/script/first_person_script.cpp
    src/script/hud_script.cpp
    src/script/input_script.cpp
    src/script/audio_script.cpp
    src/script/thing_script.cpp
)

target_link_libraries(slopengine PRIVATE
    sloplib
    flecs::flecs_static
    Jolt::Jolt
    rlImGui
    soloud
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

add_executable(slopmap
    tools/slopmap/main.cpp
    tools/slopmap/camera.cpp
    tools/slopmap/editor.cpp
    tools/slopmap/preview.cpp
    tools/slopmap/thing_draw.cpp
    tools/slopmap/create_tool.cpp
    tools/slopmap/select_tool.cpp
    tools/slopmap/place_tool.cpp
    tools/slopmap/material_browser.cpp
    tools/slopmap/prefab_browser.cpp
    tools/slopmap/layout.cpp
    src/ui/imgui_fonts.cpp
)
target_include_directories(slopmap PRIVATE tools/slopmap)
target_link_libraries(slopmap PRIVATE sloplib rlImGui)
if(MSVC)
    target_compile_options(slopmap PRIVATE /W4 /permissive-)
else()
    target_compile_options(slopmap PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(slopsprite
    tools/slopsprite/main.cpp
    tools/slopsprite/camera.cpp
    tools/slopsprite/editor.cpp
    tools/slopsprite/browser.cpp
    tools/slopsprite/world_preview.cpp
    tools/slopsprite/fp_preview.cpp
    tools/slopsprite/align_preview.cpp
    tools/slopsprite/texture_browser.cpp
    tools/slopsprite/sound_browser.cpp
    tools/slopsprite/layout.cpp
    src/ui/imgui_fonts.cpp
    src/audio/audio_world.cpp
)
target_include_directories(slopsprite PRIVATE tools/slopsprite)
target_link_libraries(slopsprite PRIVATE sloplib rlImGui soloud)
if(MSVC)
    target_compile_options(slopsprite PRIVATE /W4 /permissive-)
else()
    target_compile_options(slopsprite PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(slopicons tools/slopicons/main.cpp)
target_link_libraries(slopicons PRIVATE raylib)
if(MSVC)
    target_compile_options(slopicons PRIVATE /W4 /permissive-)
else()
    target_compile_options(slopicons PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_custom_target(pack-silk-icons
    COMMAND $<TARGET_FILE:slopicons> pack silk "${CMAKE_SOURCE_DIR}/packages/engine/icons"
    DEPENDS slopicons
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Packing packages/engine/icons/silk into silk.png + silk.iconmap"
)

install(DIRECTORY "${CMAKE_SOURCE_DIR}/packages/engine/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/slopengine/engine"
)

include(cmake/Doxygen.cmake)
