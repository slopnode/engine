cmake_minimum_required(VERSION 3.22)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# We vendor our own patched raylib fork (lib/raylib). A machine-wide `vcpkg integrate
# install` silently injects a stock vcpkg raylib's headers into every MSBuild project,
# which can shadow our fork's headers and break the build with missing-member errors.
# Opt this project's MSBuild output out of that global integration (no-op on non-VS
# generators, i.e. Linux/macOS).
set(CMAKE_VS_GLOBALS "VcpkgEnabled=false")

project(slopengine LANGUAGES C CXX)

if(WIN32)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

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

if(NOT MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -ffunction-sections -fdata-sections")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -ffunction-sections -fdata-sections")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -s -Wl,--gc-sections")
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT SLOPENGINE_IPO_SUPPORTED OUTPUT SLOPENGINE_IPO_ERROR)
if(SLOPENGINE_IPO_SUPPORTED)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
else()
    message(STATUS "LTO not supported: ${SLOPENGINE_IPO_ERROR}")
endif()

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

set(SLOPENGINE_APP_SEARCH_PATHS "" CACHE STRING
    "Semicolon-separated default directories to search for game/mod packages by name \
(e.g. --base-game <name>). A downstream build (packaging a specific game, CI, etc.) \
sets this to point at wherever it lays out game/mod directories.")

list(JOIN SLOPENGINE_APP_SEARCH_PATHS "\n" SLOPENGINE_APP_SEARCH_PATHS_JOINED)

configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/app_search_paths.hpp.in"
    "${CMAKE_BINARY_DIR}/generated/core/app_search_paths.hpp"
    @ONLY
)

add_subdirectory(lib)

set(SLOPLIB_SOURCES
    src/core/engine_package.cpp
    src/core/package.cpp
    src/core/package_meta.cpp
    src/core/package_search.cpp
    src/core/semver.cpp
    src/core/sexpr.cpp
    src/core/user_paths.cpp
    src/core/vfs.cpp
    src/script/script_scope.cpp
    src/script/package_load_context.cpp
    src/script/proc_role.cpp
    src/assets/asset_store.cpp
    src/assets/geo_loader.cpp
    src/assets/icon_atlas.cpp
    src/assets/material_loader.cpp
    src/assets/prt_loader.cpp
    src/assets/saudio_loader.cpp
    src/assets/sprite_loader.cpp
    src/assets/sprite_anim_loader.cpp
    src/assets/texture_anim_loader.cpp
    src/assets/skeleton_loader.cpp
    src/assets/anim_loader.cpp
    src/particles/particle_sim.cpp
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
    src/map/mover_brushes.cpp
    src/map/handler_binding.cpp
    src/map/map_handler_registry.cpp
    src/map/thing_def_registry.cpp
    src/map/graph_script.cpp
    src/map/lightmap.cpp
    src/map/light_occlusion.cpp
    src/map/light_probes.cpp
    src/map/light_sample.cpp
    src/map/map_meta.cpp
    src/map/prefab.cpp
    src/map/quad_bvh.cpp
    src/map/radiosity.cpp
    src/map/radiosity_emitters.cpp
    src/map/emitter_bvh.cpp
    src/map/seam_stitch.cpp
    src/map/radiosity_gpu.cpp
    src/map/radiosity_lights.cpp
    src/map/uv_math.cpp
    src/map/fac_build.cpp
    src/map/fac_io.cpp
    src/map/pvs_build.cpp
    src/map/pvs_io.cpp
    src/map/nav_graph.cpp
    src/map/sound_propagation.cpp
    src/render/sprite_billboard.cpp
    src/render/render_frustum.cpp
    src/render/skybox.cpp
    src/render/skybox_render.cpp
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
    src/core/screenshot.cpp
    src/core/frame_perf.cpp
    src/game/user_settings.cpp
    src/game/app.cpp
    src/game/game_state.cpp
    src/game/package_cli.cpp
    src/input/action_registry.cpp
    src/input/bind_code.cpp
    src/input/input_module.cpp
    src/camera/camera_module.cpp
    src/interact/interact_module.cpp
    src/ui/ui_module.cpp
    src/ui/imgui_fonts.cpp
    src/physics/physics_world.cpp
    src/physics/physics_module.cpp
    src/physics/sight_module.cpp
    src/navigation/nav_module.cpp
    src/navigation/nav_script.cpp
    src/physics/motored_body.cpp
    src/physics/motored_sweep.cpp
    src/physics/rigid_mover.cpp
    src/physics/rigid_mover_motion.cpp
    src/physics/map_collision.cpp
    src/particles/particle_module.cpp
    src/fx/trail.cpp
    src/render/view_sprite_attachment.cpp
    src/audio/audio_world.cpp
    src/audio/audio_module.cpp
    src/map/things_spawn.cpp
    src/map/brush_door.cpp
    src/map/face_triggers.cpp
    src/map/map_scene.cpp
    src/game/script_boot.cpp
    src/game/menu_background.cpp
    src/render/transform.cpp
    src/render/animation_player.cpp
    src/render/sprite_animator.cpp
    src/render/sprite_animator_system.cpp
    src/render/material_anim.cpp
    src/render/animation_systems.cpp
    src/render/dynamic_light.cpp
    src/render/dynamic_light_compositing.cpp
    src/render/dynamic_light_shadow_math.cpp
    src/render/dynamic_light_shadows.cpp
    src/render/fx_local_light.cpp
    src/render/render_debug.cpp
    src/render/render_pass_world.cpp
    src/render/render_pass_fp.cpp
    src/render/render_pass_extra_eye.cpp
    src/render/post_process.cpp
    src/render/skybox_world.cpp
    src/render/render_module.cpp
    src/render/hud.cpp
    src/script/scheme_call.cpp
    src/script/scheme_harden.cpp
    src/script/hook_registry.cpp
    src/script/first_person_script.cpp
    src/script/hud_script.cpp
    src/script/post_script.cpp
    src/script/input_script.cpp
    src/script/audio_script.cpp
    src/script/thing_script.cpp
    src/script/save_script.cpp
    src/script/ui_script.cpp
)

target_link_libraries(slopengine PRIVATE
    sloplib
    flecs::flecs_static
    Jolt::Jolt
    rlImGui
    soloud
)

if(SLOPENGINE_HAS_STEAM_AUDIO)
    target_sources(slopengine PRIVATE
        src/audio/steam_audio_runtime.cpp
        src/audio/steam_audio_scene.cpp
        src/audio/steam_audio_spatialize_filter.cpp
    )
    target_compile_definitions(slopengine PRIVATE SLOPENGINE_HAS_STEAM_AUDIO=1)
    target_link_libraries(slopengine PRIVATE SteamAudio::phonon)
    set_target_properties(slopengine PROPERTIES
        BUILD_RPATH "${STEAM_AUDIO_LIB_DIR}"
        INSTALL_RPATH "${STEAM_AUDIO_LIB_DIR}"
    )
endif()

if(MSVC)
    target_compile_options(slopengine PRIVATE /W4 /permissive-)
else()
    target_compile_options(slopengine PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(sloprepl tools/sloprepl/main.cpp)
target_link_libraries(sloprepl PRIVATE sloplib)
if(MSVC)
    target_compile_options(sloprepl PRIVATE /W4 /permissive-)
else()
    target_compile_options(sloprepl PRIVATE -Wall -Wextra -Wpedantic)
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
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(sloprad PRIVATE tools/sloprad/headless_gl_context.cpp)
    target_link_libraries(sloprad PRIVATE ${CMAKE_DL_LIBS})
endif()
if(MSVC)
    target_compile_options(sloprad PRIVATE /W4 /permissive-)
else()
    target_compile_options(sloprad PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(slopfac tools/slopfac/main.cpp)
target_link_libraries(slopfac PRIVATE sloplib)
if(MSVC)
    target_compile_options(slopfac PRIVATE /W4 /permissive-)
else()
    target_compile_options(slopfac PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(slopvis tools/slopvis/main.cpp)
target_link_libraries(slopvis PRIVATE sloplib)
if(MSVC)
    target_compile_options(slopvis PRIVATE /W4 /permissive-)
else()
    target_compile_options(slopvis PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(slopmap
    tools/slopmap/main.cpp
    tools/slopmap/camera.cpp
    tools/slopmap/editor.cpp
    tools/slopmap/history.cpp
    tools/slopmap/preview.cpp
    tools/slopmap/thing_draw.cpp
    tools/slopmap/create_tool.cpp
    tools/slopmap/select_tool.cpp
    tools/slopmap/place_tool.cpp
    tools/slopmap/punch_tool.cpp
    tools/slopmap/clip_tool.cpp
    tools/slopmap/material_browser.cpp
    tools/slopmap/material_thumb_atlas.cpp
    tools/slopmap/editor_settings.cpp
    tools/slopmap/texture_panel.cpp
    tools/slopmap/thing_panel.cpp
    tools/slopmap/particle_preview.cpp
    tools/slopmap/brush_panel.cpp
    tools/slopmap/handler_ui.cpp
    tools/slopmap/prefab_browser.cpp
    tools/slopmap/layout.cpp
    tools/slopmap/compile.cpp
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

enable_testing()
add_subdirectory(test)

include(cmake/Doxygen.cmake)
