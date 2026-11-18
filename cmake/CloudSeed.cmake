# CloudSeedCore (Ghost Note Audio) has no CMake build of its own, so it is
# compiled here as a small static library. Only the reverb DSP is used; the
# upstream demo program (PluginProcessor.cpp) is left out.

set(_cloudseed_dir "${CMAKE_CURRENT_SOURCE_DIR}/external/CloudSeedCore")

add_library(thunder_cloudseed STATIC
    "${_cloudseed_dir}/Parameters.cpp"
    "${_cloudseed_dir}/DSP/Biquad.cpp"
    "${_cloudseed_dir}/DSP/RandomBuffer.cpp")

target_include_directories(thunder_cloudseed SYSTEM PUBLIC "${_cloudseed_dir}")

# BUFFER_SIZE is the largest block the reverb processes at once. Thunder's engine
# always processes in chunks of at most 256 samples, so this matches that limit.
target_compile_definitions(thunder_cloudseed PUBLIC
    BUFFER_SIZE=256
    MAX_STR_SIZE=32)

# CloudSeed's headers use MSVC's strcpy_s. Everywhere else, force-include a
# portable stand-in — PUBLIC, because its headers are included by the code that
# uses the reverb (and inline functions in them call strcpy_s too).
if(NOT MSVC)
    target_compile_options(thunder_cloudseed PUBLIC
        "SHELL:-include \"${CMAKE_CURRENT_SOURCE_DIR}/cmake/compat/CloudSeedCompat.h\"")
endif()

set_target_properties(thunder_cloudseed PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    FOLDER "external")

if(MSVC)
    # Third-party code: keep its warnings out of the build log.
    target_compile_options(thunder_cloudseed PRIVATE /W0)
else()
    target_compile_options(thunder_cloudseed PRIVATE -w)
endif()
