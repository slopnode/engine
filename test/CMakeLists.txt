add_executable(sloptests
    test_main.cpp
    test_sexpr.cpp
    test_assets.cpp
    test_script_scope.cpp
    test_scheme_harden.cpp
    test_bsp_build.cpp
    test_bsp_analyze.cpp
    test_fac_build.cpp
    test_pvs_build.cpp
    test_physics.cpp
    test_frustum.cpp
    test_transform.cpp
    ${CMAKE_SOURCE_DIR}/src/physics/physics_world.cpp
    ${CMAKE_SOURCE_DIR}/src/physics/rigid_mover_motion.cpp
    ${CMAKE_SOURCE_DIR}/src/script/scheme_harden.cpp
    ${CMAKE_SOURCE_DIR}/src/render/transform.cpp
)

target_include_directories(sloptests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(sloptests PRIVATE sloplib Jolt::Jolt flecs::flecs_static)

if(MSVC)
    target_compile_options(sloptests PRIVATE /W4 /permissive-)
else()
    target_compile_options(sloptests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME sloptests.sexpr COMMAND sloptests sexpr)
add_test(NAME sloptests.assets COMMAND sloptests assets)
add_test(NAME sloptests.script_scope COMMAND sloptests script_scope)
add_test(NAME sloptests.scheme_harden COMMAND sloptests scheme_harden)
add_test(NAME sloptests.bsp_build COMMAND sloptests bsp_build)
add_test(NAME sloptests.bsp_analyze COMMAND sloptests bsp_analyze)
add_test(NAME sloptests.fac_build COMMAND sloptests fac_build)
add_test(NAME sloptests.pvs_build COMMAND sloptests pvs_build)
add_test(NAME sloptests.physics COMMAND sloptests physics)
add_test(NAME sloptests.frustum COMMAND sloptests frustum)
add_test(NAME sloptests.transform COMMAND sloptests transform)
