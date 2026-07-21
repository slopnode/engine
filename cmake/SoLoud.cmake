set(SOLOUD_ROOT "${CMAKE_SOURCE_DIR}/lib/soloud")
set(SOLOUD_SRC "${SOLOUD_ROOT}/src")
set(SOLOUD_INC "${SOLOUD_ROOT}/include")

set(SOLOUD_CORE_SOURCES
    ${SOLOUD_SRC}/core/soloud.cpp
    ${SOLOUD_SRC}/core/soloud_audiosource.cpp
    ${SOLOUD_SRC}/core/soloud_bus.cpp
    ${SOLOUD_SRC}/core/soloud_core_3d.cpp
    ${SOLOUD_SRC}/core/soloud_core_basicops.cpp
    ${SOLOUD_SRC}/core/soloud_core_faderops.cpp
    ${SOLOUD_SRC}/core/soloud_core_filterops.cpp
    ${SOLOUD_SRC}/core/soloud_core_getters.cpp
    ${SOLOUD_SRC}/core/soloud_core_setters.cpp
    ${SOLOUD_SRC}/core/soloud_core_voicegroup.cpp
    ${SOLOUD_SRC}/core/soloud_core_voiceops.cpp
    ${SOLOUD_SRC}/core/soloud_fader.cpp
    ${SOLOUD_SRC}/core/soloud_fft.cpp
    ${SOLOUD_SRC}/core/soloud_fft_lut.cpp
    ${SOLOUD_SRC}/core/soloud_file.cpp
    ${SOLOUD_SRC}/core/soloud_filter.cpp
    ${SOLOUD_SRC}/core/soloud_misc.cpp
    ${SOLOUD_SRC}/core/soloud_queue.cpp
    ${SOLOUD_SRC}/core/soloud_thread.cpp
)

set(SOLOUD_WAV_SOURCES
    ${SOLOUD_SRC}/audiosource/wav/dr_impl.cpp
    ${SOLOUD_SRC}/audiosource/wav/soloud_wav.cpp
    ${SOLOUD_SRC}/audiosource/wav/soloud_wavstream.cpp
    ${SOLOUD_SRC}/audiosource/wav/stb_vorbis.c
)

set(SOLOUD_SFXR_SOURCES
    ${SOLOUD_SRC}/audiosource/sfxr/soloud_sfxr.cpp
)

set(SOLOUD_FILTER_SOURCES
    ${SOLOUD_SRC}/filter/soloud_bassboostfilter.cpp
    ${SOLOUD_SRC}/filter/soloud_biquadresonantfilter.cpp
    ${SOLOUD_SRC}/filter/soloud_dcremovalfilter.cpp
    ${SOLOUD_SRC}/filter/soloud_duckfilter.cpp
    ${SOLOUD_SRC}/filter/soloud_echofilter.cpp
    ${SOLOUD_SRC}/filter/soloud_eqfilter.cpp
    ${SOLOUD_SRC}/filter/soloud_fftfilter.cpp
    ${SOLOUD_SRC}/filter/soloud_flangerfilter.cpp
    ${SOLOUD_SRC}/filter/soloud_freeverbfilter.cpp
    ${SOLOUD_SRC}/filter/soloud_lofifilter.cpp
    ${SOLOUD_SRC}/filter/soloud_robotizefilter.cpp
    ${SOLOUD_SRC}/filter/soloud_waveshaperfilter.cpp
)

set(SOLOUD_BACKEND_SOURCES
    ${SOLOUD_SRC}/backend/miniaudio/soloud_miniaudio.cpp
)

add_library(soloud STATIC
    ${SOLOUD_CORE_SOURCES}
    ${SOLOUD_WAV_SOURCES}
    ${SOLOUD_SFXR_SOURCES}
    ${SOLOUD_FILTER_SOURCES}
    ${SOLOUD_BACKEND_SOURCES}
)

target_include_directories(soloud PUBLIC ${SOLOUD_INC})
target_include_directories(soloud PRIVATE
    ${SOLOUD_SRC}/backend/miniaudio
    ${SOLOUD_SRC}/audiosource/wav
)
target_compile_definitions(soloud PUBLIC WITH_MINIAUDIO)

set_target_properties(soloud PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)

if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(soloud PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)
endif()

if(MSVC)
    target_compile_options(soloud PRIVATE /W3)
    set_property(TARGET soloud PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
else()
    target_compile_options(soloud PRIVATE -w)
endif()
