set(FLECS_STATIC ON CACHE BOOL "" FORCE)
set(FLECS_SHARED OFF CACHE BOOL "" FORCE)
set(FLECS_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(flecs)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(raylib)
target_compile_definitions(raylib PUBLIC SUPPORT_GPU_SKINNING=1)

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
    foreach(lib_target flecs_static raylib s7 glfw)
        if(TARGET ${lib_target})
            set_property(TARGET ${lib_target} PROPERTY
                MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
        endif()
    endforeach()
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(s7 PUBLIC m dl)
endif()
