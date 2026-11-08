add_executable(sloptests
    test_main.cpp
    test_sexpr.cpp
    test_package_search.cpp
    test_package_meta.cpp
    test_assets.cpp
    test_rigged_assets.cpp
    test_texture_graph.cpp
    test_script_scope.cpp
    test_scheme_harden.cpp
    test_proc_role.cpp
    test_sim_clock.cpp
    test_net.cpp
    test_transport.cpp
    test_enet_transport.cpp
    test_secure_transport.cpp
    test_net_protocol.cpp
    test_net_session.cpp
    test_net_prediction.cpp
    test_net_interest.cpp
    test_net_hardening.cpp
    test_net_pacing.cpp
    test_s7_read.cpp
    test_bsp_build.cpp
    test_bsp_analyze.cpp
    test_bsp_polygon.cpp
    test_map_script.cpp
    test_camera_path.cpp
    test_csg_compile.cpp
    test_pvs_build.cpp
    test_map_areas.cpp
    test_map_meta.cpp
    test_nav_graph.cpp
    test_nav_macro_links.cpp
    test_nav_bake.cpp
    test_nav_navmesh_build.cpp
    test_nav_io.cpp
    test_nav_profile_registry.cpp
    test_physics.cpp
    test_frustum.cpp
    test_transform.cpp
    test_dynamic_light.cpp
    test_dynamic_light_compositing.cpp
    test_brush_door.cpp
    test_brush_split.cpp
    test_brush_extrude.cpp
    test_brush_carve.cpp
    test_brush_role.cpp
    test_brush_blocks.cpp
    test_lightmap_transparent.cpp
    test_lightmap_rgbe.cpp
    test_lightmap_merge.cpp
    test_lightmap_pack.cpp
    test_radiosity_emitters.cpp
    test_sun_shadow_softness.cpp
    test_light_probe_cube.cpp
    test_attachment_path.cpp
    test_seam_stitch.cpp
    test_sight.cpp
    test_view_recoil.cpp
    test_model_hitboxes.cpp
    test_skin_cut.cpp
    test_ragdoll.cpp
    test_slopui_theme.cpp
    test_tracer.cpp
    ${CMAKE_SOURCE_DIR}/src/physics/physics_world.cpp
    ${CMAKE_SOURCE_DIR}/src/physics/motored_sweep.cpp
    ${CMAKE_SOURCE_DIR}/src/physics/rigid_mover_motion.cpp
    ${CMAKE_SOURCE_DIR}/src/map/nav_macro_links.cpp
    ${CMAKE_SOURCE_DIR}/src/map/brush_door.cpp
    ${CMAKE_SOURCE_DIR}/src/map/area_gate_runtime.cpp
    ${CMAKE_SOURCE_DIR}/src/script/scheme_harden.cpp
    ${CMAKE_SOURCE_DIR}/src/script/hook_registry.cpp
    ${CMAKE_SOURCE_DIR}/src/game/sim_pipeline.cpp
    ${CMAKE_SOURCE_DIR}/src/game/sim_interpolation.cpp
    ${CMAKE_SOURCE_DIR}/src/net/net_state.cpp
    ${CMAKE_SOURCE_DIR}/src/net/net_session.cpp
    ${CMAKE_SOURCE_DIR}/src/map/player_bodies.cpp
    ${CMAKE_SOURCE_DIR}/src/script/net_script.cpp
    ${CMAKE_SOURCE_DIR}/src/script/player_target.cpp
    ${CMAKE_SOURCE_DIR}/src/render/transform.cpp
    ${CMAKE_SOURCE_DIR}/src/render/animation_player.cpp
    ${CMAKE_SOURCE_DIR}/src/camera/view_recoil.cpp
    ${CMAKE_SOURCE_DIR}/src/fx/tracer_motion.cpp
    ${CMAKE_SOURCE_DIR}/src/physics/ragdoll_pose.cpp
    ${CMAKE_SOURCE_DIR}/src/render/attachment_path.cpp
    ${CMAKE_SOURCE_DIR}/src/render/dynamic_light.cpp
    ${CMAKE_SOURCE_DIR}/src/render/dynamic_light_compositing.cpp
    ${CMAKE_SOURCE_DIR}/src/render/dynamic_light_shadow_math.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/slopui_theme_data.cpp
)

target_include_directories(sloptests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(sloptests PRIVATE sloplib Jolt::Jolt flecs::flecs_static imgui)

if(MSVC)
    target_compile_options(sloptests PRIVATE /W4 /permissive-)
else()
    target_compile_options(sloptests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME sloptests.sexpr COMMAND sloptests sexpr)
add_test(NAME sloptests.package_search COMMAND sloptests package_search)
add_test(NAME sloptests.package_meta COMMAND sloptests package_meta)
add_test(NAME sloptests.assets COMMAND sloptests assets)
add_test(NAME sloptests.rigged_assets COMMAND sloptests rigged_assets)
add_test(NAME sloptests.texture_graph COMMAND sloptests texture_graph)
add_test(NAME sloptests.script_scope COMMAND sloptests script_scope)
add_test(NAME sloptests.proc_role COMMAND sloptests proc_role)
add_test(NAME sloptests.sim_clock COMMAND sloptests sim_clock)
add_test(NAME sloptests.net COMMAND sloptests net)
add_test(NAME sloptests.transport COMMAND sloptests transport)
add_test(NAME sloptests.enet_transport COMMAND sloptests enet_transport)
add_test(NAME sloptests.secure_transport COMMAND sloptests secure_transport)
add_test(NAME sloptests.net_protocol COMMAND sloptests net_protocol)
add_test(NAME sloptests.net_session COMMAND sloptests net_session)
add_test(NAME sloptests.net_prediction COMMAND sloptests net_prediction)
add_test(NAME sloptests.net_interest COMMAND sloptests net_interest)
add_test(NAME sloptests.net_hardening COMMAND sloptests net_hardening)
add_test(NAME sloptests.net_pacing COMMAND sloptests net_pacing)
add_test(NAME sloptests.scheme_harden COMMAND sloptests scheme_harden)
add_test(NAME sloptests.bsp_build COMMAND sloptests bsp_build)
add_test(NAME sloptests.light_probe_cube COMMAND sloptests light_probe_cube)
add_test(NAME sloptests.attachment_path COMMAND sloptests attachment_path)
add_test(NAME sloptests.bsp_analyze COMMAND sloptests bsp_analyze)
add_test(NAME sloptests.map_script COMMAND sloptests map_script)
add_test(NAME sloptests.camera_path COMMAND sloptests camera_path)
add_test(NAME sloptests.csg_compile COMMAND sloptests csg_compile)
add_test(NAME sloptests.pvs_build COMMAND sloptests pvs_build)
add_test(NAME sloptests.map_areas COMMAND sloptests map_areas)
add_test(NAME sloptests.nav_graph COMMAND sloptests nav_graph)
add_test(NAME sloptests.nav_bake COMMAND sloptests nav_bake)
add_test(NAME sloptests.nav_navmesh_build COMMAND sloptests nav_navmesh_build)
add_test(NAME sloptests.nav_io COMMAND sloptests nav_io)
add_test(NAME sloptests.nav_profile_registry COMMAND sloptests nav_profile_registry)
add_test(NAME sloptests.physics COMMAND sloptests physics)
add_test(NAME sloptests.frustum COMMAND sloptests frustum)
add_test(NAME sloptests.transform COMMAND sloptests transform)
add_test(NAME sloptests.dynamic_light COMMAND sloptests dynamic_light)
add_test(NAME sloptests.dynamic_light_compositing COMMAND sloptests dynamic_light_compositing)
add_test(NAME sloptests.brush_door COMMAND sloptests brush_door)
add_test(NAME sloptests.brush_split COMMAND sloptests brush_split)
add_test(NAME sloptests.brush_carve COMMAND sloptests brush_carve)
add_test(NAME sloptests.brush_role COMMAND sloptests brush_role)
add_test(NAME sloptests.brush_blocks COMMAND sloptests brush_blocks)
add_test(NAME sloptests.lightmap_transparent COMMAND sloptests lightmap_transparent)
add_test(NAME sloptests.lightmap_rgbe COMMAND sloptests lightmap_rgbe)
add_test(NAME sloptests.lightmap_pack COMMAND sloptests lightmap_pack)
add_test(NAME sloptests.radiosity_emitters COMMAND sloptests radiosity_emitters)
add_test(NAME sloptests.sun_shadow_softness COMMAND sloptests sun_shadow_softness)
add_test(NAME sloptests.sight COMMAND sloptests sight)
add_test(NAME sloptests.view_recoil COMMAND sloptests view_recoil)
add_test(NAME sloptests.model_hitboxes COMMAND sloptests model_hitboxes)
add_test(NAME sloptests.skin_cut COMMAND sloptests skin_cut)
add_test(NAME sloptests.ragdoll COMMAND sloptests ragdoll)
add_test(NAME sloptests.slopui_theme COMMAND sloptests slopui_theme)
add_test(NAME sloptests.tracer COMMAND sloptests tracer)

add_test(NAME slopdocs.fixture
    COMMAND slopdocs "${CMAKE_SOURCE_DIR}/test/fixtures/slopdocs-package" --out "${CMAKE_BINARY_DIR}/test/slopdocs-fixture"
)
