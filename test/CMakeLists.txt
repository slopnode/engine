add_executable(sloptests
    test_main.cpp
    test_sexpr.cpp
    test_assets.cpp
    test_script_scope.cpp
    test_scheme_harden.cpp
    test_bsp_build.cpp
    test_bsp_analyze.cpp
    test_vis_build.cpp
    ${CMAKE_SOURCE_DIR}/src/script/scheme_harden.cpp
)

target_include_directories(sloptests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(sloptests PRIVATE sloplib)

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
add_test(NAME sloptests.vis_build COMMAND sloptests vis_build)
