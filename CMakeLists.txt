cmake_minimum_required(VERSION 3.24)

# macOS: universal binary (Apple silicon + Intel), macOS 11 or newer.
# These must be set before project().
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures to build")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(Thunder VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Group targets into folders in IDEs that support it.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
if(APPLE)
    set(_thunder_default_formats "VST3;AU;Standalone")
else()
    # Windows and Linux. LV2 is added later for Linux.
    set(_thunder_default_formats "VST3;Standalone")
endif()

set(THUNDER_FORMATS "${_thunder_default_formats}" CACHE STRING "Plugin formats passed to juce_add_plugin")
option(THUNDER_COPY_AFTER_BUILD "Copy built plugins into the system plugin folders" OFF)

# ---------------------------------------------------------------------------
# Dependencies (git submodules, pinned commits — see external/)
# ---------------------------------------------------------------------------
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE/CMakeLists.txt")
    message(FATAL_ERROR
        "Dependencies are missing. Run:\n"
        "  git submodule update --init --recursive --depth 1")
endif()

add_subdirectory(external/JUCE)

# Signalsmith Stretch looks for an existing signalsmith-linear target before
# trying to download one, so adding the pinned submodule first keeps builds offline.
add_subdirectory(external/signalsmith-linear EXCLUDE_FROM_ALL)
add_subdirectory(external/signalsmith-stretch EXCLUDE_FROM_ALL)

include(cmake/CloudSeed.cmake)
include(cmake/CompilerSettings.cmake)

# ---------------------------------------------------------------------------
# Thunder
# ---------------------------------------------------------------------------
add_subdirectory(source)
