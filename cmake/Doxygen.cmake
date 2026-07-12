option(SLOPENGINE_BUILD_DOCS "Build HTML API documentation with Doxygen" OFF)

if(SLOPENGINE_BUILD_DOCS)
    find_package(Doxygen REQUIRED)
else()
    find_package(Doxygen QUIET)
endif()

if(DOXYGEN_FOUND)
    set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/docs")
    set(DOXYGEN_INPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/src")
    set(DOXYGEN_MAINPAGE "${CMAKE_SOURCE_DIR}/docs/mainpage.md")

    configure_file(
        "${CMAKE_SOURCE_DIR}/docs/Doxyfile.in"
        "${CMAKE_BINARY_DIR}/Doxyfile"
        @ONLY
    )

    add_custom_target(docs
        COMMAND "${DOXYGEN_EXECUTABLE}" "${CMAKE_BINARY_DIR}/Doxyfile"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        COMMENT "Generating HTML documentation with Doxygen"
        VERBATIM
    )
endif()
