cmake_minimum_required(VERSION 3.24)

# macOS: universal binary (Apple silicon + Intel), macOS 11 or newer.
# These must be set before project().
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures to build")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(Thunder VERSION 0.5.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Group targets into folders in IDEs that support it.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
# Plugin formats built by juce_add_plugin. CLAP is added separately (below),
# LV2 on Linux later.
if(APPLE)
    set(_thunder_default_formats "VST3;AU")
else()
    set(_thunder_default_formats "VST3")
endif()

set(THUNDER_FORMATS "${_thunder_default_formats}" CACHE STRING "Plugin formats passed to juce_add_plugin")
option(THUNDER_COPY_AFTER_BUILD "Copy built plugins into the system plugin folders" OFF)
option(THUNDER_BUILD_TESTS "Build the ThunderTests test runner" ON)

# Must come before any target (dependencies included) is created.
include(cmake/Sanitizers.cmake)

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

# Treat Signalsmith's headers as third-party (system) headers so Thunder's strict
# warning settings don't apply to them.
target_include_directories(signalsmith-linear SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/external/signalsmith-linear/include")
target_include_directories(signalsmith-stretch SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/external/signalsmith-stretch/include")

# CLAP, through clap-juce-extensions (PRD NFR-COMP-001).
option(THUNDER_BUILD_CLAP "Build the CLAP plugin" ON)
if(THUNDER_BUILD_CLAP)
    add_subdirectory(external/clap-juce-extensions EXCLUDE_FROM_ALL)
endif()

include(cmake/CloudSeed.cmake)
include(cmake/CompilerSettings.cmake)

# ---------------------------------------------------------------------------
# Thunder
# ---------------------------------------------------------------------------
add_subdirectory(source)

if(THUNDER_BUILD_TESTS)
    add_subdirectory(external/Catch2 EXCLUDE_FROM_ALL)
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/external/Catch2/extras")
    enable_testing()
    add_subdirectory(tests)
endif()
