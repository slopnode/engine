option(SLOPENGINE_USE_STEAM_AUDIO "Enable Steam Audio spatialization (requires external SDK)" OFF)
set(STEAM_AUDIO_ROOT "" CACHE PATH "Path to Steam Audio SDK root (binary zip or source checkout)")

if(NOT SLOPENGINE_USE_STEAM_AUDIO)
    set(SLOPENGINE_HAS_STEAM_AUDIO OFF CACHE INTERNAL "Steam Audio linked into slopengine" FORCE)
    unset(STEAM_AUDIO_LIB_DIR CACHE)
    return()
endif()

if(STEAM_AUDIO_ROOT STREQUAL "")
    message(FATAL_ERROR
        "SLOPENGINE_USE_STEAM_AUDIO is ON but STEAM_AUDIO_ROOT is empty.\n"
        "Set -DSTEAM_AUDIO_ROOT=/path/to/steamaudio (binary SDK zip extract, or a source checkout).")
endif()

if(NOT EXISTS "${STEAM_AUDIO_ROOT}")
    message(FATAL_ERROR "STEAM_AUDIO_ROOT does not exist: ${STEAM_AUDIO_ROOT}")
endif()

set(_steam_audio_include "")
if(EXISTS "${STEAM_AUDIO_ROOT}/include/phonon.h")
    set(_steam_audio_include "${STEAM_AUDIO_ROOT}/include")
elseif(EXISTS "${STEAM_AUDIO_ROOT}/fmod/include/phonon/phonon.h")
    set(_steam_audio_include "${STEAM_AUDIO_ROOT}/fmod/include/phonon")
elseif(EXISTS "${STEAM_AUDIO_ROOT}/core/src/core/phonon.h")
    set(_steam_audio_include "${STEAM_AUDIO_ROOT}/core/src/core")
else()
    message(FATAL_ERROR
        "Could not find phonon.h under STEAM_AUDIO_ROOT=${STEAM_AUDIO_ROOT}.\n"
        "Expected include/phonon.h (binary SDK) or fmod/include/phonon/phonon.h (source checkout).")
endif()

if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_steam_audio_lib_subdir "lib/windows-x64")
    else()
        set(_steam_audio_lib_subdir "lib/windows-x86")
    endif()
    set(_steam_audio_lib_names phonon)
elseif(APPLE)
    set(_steam_audio_lib_subdir "lib/osx")
    set(_steam_audio_lib_names phonon)
else()
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_steam_audio_lib_subdir "lib/linux-x64")
    else()
        set(_steam_audio_lib_subdir "lib/linux-x86")
    endif()
    set(_steam_audio_lib_names phonon)
endif()

set(_steam_audio_lib_dir "${STEAM_AUDIO_ROOT}/${_steam_audio_lib_subdir}")
if(NOT EXISTS "${_steam_audio_lib_dir}")
    if(EXISTS "${STEAM_AUDIO_ROOT}/unity/src/project/SteamAudioUnity/Assets/Plugins/SteamAudio/Binaries/Linux/x86_64")
        set(_steam_audio_lib_dir "${STEAM_AUDIO_ROOT}/unity/src/project/SteamAudioUnity/Assets/Plugins/SteamAudio/Binaries/Linux/x86_64")
    endif()
endif()

find_library(STEAM_AUDIO_LIBRARY
    NAMES ${_steam_audio_lib_names}
    PATHS "${_steam_audio_lib_dir}"
    NO_DEFAULT_PATH
)

if(NOT STEAM_AUDIO_LIBRARY)
    message(FATAL_ERROR
        "SLOPENGINE_USE_STEAM_AUDIO is ON but libphonon was not found.\n"
        "Looked in: ${_steam_audio_lib_dir}\n"
        "Point STEAM_AUDIO_ROOT at a binary SDK extract that contains ${_steam_audio_lib_subdir}/, "
        "or place libphonon there. Source checkouts do not ship prebuilt libs unless you build them.")
endif()

add_library(SteamAudio::phonon SHARED IMPORTED GLOBAL)
set_target_properties(SteamAudio::phonon PROPERTIES
    IMPORTED_LOCATION "${STEAM_AUDIO_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${_steam_audio_include}"
)

if(WIN32)
    get_filename_component(_steam_audio_lib_file "${STEAM_AUDIO_LIBRARY}" NAME_WE)
    set(_steam_audio_dll "${_steam_audio_lib_dir}/${_steam_audio_lib_file}.dll")
    if(EXISTS "${_steam_audio_dll}")
        set_target_properties(SteamAudio::phonon PROPERTIES IMPORTED_LOCATION "${_steam_audio_dll}")
        set_target_properties(SteamAudio::phonon PROPERTIES IMPORTED_IMPLIB "${STEAM_AUDIO_LIBRARY}")
    endif()
endif()

set(STEAM_AUDIO_LIB_DIR "${_steam_audio_lib_dir}" CACHE INTERNAL "Steam Audio library directory for RPATH")
set(SLOPENGINE_HAS_STEAM_AUDIO ON CACHE INTERNAL "Steam Audio linked into slopengine" FORCE)

message(STATUS "Steam Audio: enabled")
message(STATUS "  include: ${_steam_audio_include}")
message(STATUS "  library: ${STEAM_AUDIO_LIBRARY}")
