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

    file(READ "${CMAKE_SOURCE_DIR}/VERSION" SLOPENGINE_DOCS_VERSION)
    string(STRIP "${SLOPENGINE_DOCS_VERSION}" SLOPENGINE_DOCS_VERSION)

    set(SLOPENGINE_DOCS_GIT_COMMIT "unknown")
    find_package(Git QUIET)
    if(GIT_EXECUTABLE)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            OUTPUT_VARIABLE SLOPENGINE_DOCS_GIT_COMMIT
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE _slopengine_git_result
        )
        if(NOT _slopengine_git_result EQUAL 0)
            set(SLOPENGINE_DOCS_GIT_COMMIT "unknown")
        else()
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" status --porcelain
                WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                OUTPUT_VARIABLE _slopengine_git_dirty
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            if(NOT _slopengine_git_dirty STREQUAL "")
                set(SLOPENGINE_DOCS_GIT_COMMIT "${SLOPENGINE_DOCS_GIT_COMMIT}-dirty")
            endif()
        endif()
    endif()

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
