# CloudSeedCore (Ghost Note Audio) has no CMake build of its own, so it is
# compiled here as a small static library. Only the reverb DSP is used; the
# upstream demo program (PluginProcessor.cpp) is left out.
#
# Thunder builds a lightly patched copy, generated here from the pinned
# submodule (which itself is never modified):
#
#   DSP/ReverbChannel.h — UpdateLines() gets its per-line random values from
#   RandomBuffer::Generate(), which returns a std::vector. UpdateLines() runs
#   whenever the decay changes, so turning the Decay knob would allocate memory
#   on the audio thread. The patch computes the same values into a fixed-size
#   array instead (same generator, same formula).
#
#   DSP/ModulatedDelay.h, DSP/ModulatedAllpass.h — the starting modulation
#   phases came from std::rand(), a generator shared by everything in the
#   process, so no two reverbs started alike and a reopened session didn't sound
#   identical. They now come from a generator Thunder reseeds before building
#   each reverb (cmake/compat/CloudSeedDeterministic.h).
#
#   DSP/ModulatedDelay.h, DSP/ModulatedAllpass.h, DSP/MultitapDelay.h — ClearBuffers()
#   only zeroed the delay memory, so after a reset the modulation carried on from
#   wherever it was and a reset reverb didn't sound like a fresh one. It now also
#   returns each modulation to its starting phase. And Thunder can ask it to skip
#   the zeroing (Cloudseed::ThunderSkipDelayZeroing): clearing 2 s of 192 kHz
#   memory per line takes 7–10 ms, needless when the reverb is already silent
#   (PRD DEC-043).
#
# If an update to the submodule changes the patched code, configuring fails
# here rather than silently building an unpatched reverb.

set(_cloudseed_source "${CMAKE_CURRENT_SOURCE_DIR}/external/CloudSeedCore")
set(_cloudseed_dir "${CMAKE_BINARY_DIR}/cloudseed")
set(_cloudseed_patched_files DSP/ReverbChannel.h DSP/ModulatedDelay.h DSP/ModulatedAllpass.h DSP/MultitapDelay.h)

file(COPY "${_cloudseed_source}/Parameters.h" "${_cloudseed_source}/Parameters.cpp" "${_cloudseed_source}/Programs.h"
     "${_cloudseed_source}/LICENSE"
     DESTINATION "${_cloudseed_dir}")
file(COPY "${_cloudseed_source}/DSP" DESTINATION "${_cloudseed_dir}"
     PATTERN "ReverbChannel.h" EXCLUDE
     PATTERN "ModulatedDelay.h" EXCLUDE
     PATTERN "ModulatedAllpass.h" EXCLUDE
     PATTERN "MultitapDelay.h" EXCLUDE)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/compat/CloudSeedDeterministic.h"
               "${_cloudseed_dir}/DSP/ThunderDeterministic.h" COPYONLY)

# Replaces `from` with `to` in the text of a file being patched (_patch_text).
function(_thunder_cloudseed_replace file from to)
    string(FIND "${_patch_text}" "${from}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR "CloudSeedCore has changed: cmake/CloudSeed.cmake can no longer patch ${file}. "
                            "Review the patch against the new code.")
    endif()
    string(REPLACE "${from}" "${to}" _patched "${_patch_text}")
    set(_patch_text "${_patched}" PARENT_SCOPE)
endfunction()

# Writes the patched text via configure_file so the file's timestamp only
# changes when its content does (no needless rebuilds on every configure).
function(_thunder_cloudseed_write file)
    file(WRITE "${CMAKE_BINARY_DIR}/cloudseed-patch/${file}" "${_patch_text}")
    configure_file("${CMAKE_BINARY_DIR}/cloudseed-patch/${file}" "${_cloudseed_dir}/${file}" COPYONLY)
endfunction()

# ---- DSP/ReverbChannel.h: allocation-free decay changes --------------------------------------------------------------
file(READ "${_cloudseed_source}/DSP/ReverbChannel.h" _patch_text)

_thunder_cloudseed_replace(DSP/ReverbChannel.h
    [=[#include "RandomBuffer.h"]=]
    [=[#include "RandomBuffer.h"
#include "LcgRandom.h" // Thunder patch (see cmake/CloudSeed.cmake)
#include <climits>]=])

_thunder_cloudseed_replace(DSP/ReverbChannel.h
    [=[auto delayLineSeeds = RandomBuffer::Generate(delayLineSeed, TotalLineCount * 3, crossSeed);]=]
    [=[// Thunder patch (see cmake/CloudSeed.cmake): the values of
			// RandomBuffer::Generate(delayLineSeed, TotalLineCount * 3, crossSeed),
			// computed without allocating.
			float delayLineSeeds[TotalLineCount * 3];
			{
				LcgRandom seriesA((uint64_t)delayLineSeed);
				LcgRandom seriesB(~(uint64_t)delayLineSeed);
				for (int s = 0; s < TotalLineCount * 3; s++)
				{
					float a = seriesA.NextUInt() / (float)UINT_MAX;
					float b = seriesB.NextUInt() / (float)UINT_MAX;
					delayLineSeeds[s] = a * (1 - crossSeed) + b * crossSeed;
				}
			}]=])

_thunder_cloudseed_write(DSP/ReverbChannel.h)

# ---- DSP/ModulatedDelay.h and DSP/ModulatedAllpass.h: deterministic start phases ------------------------------------
foreach(_file DSP/ModulatedDelay.h DSP/ModulatedAllpass.h)
    file(READ "${_cloudseed_source}/${_file}" _patch_text)

    _thunder_cloudseed_replace(${_file}
        [=[#include "Utils.h"]=]
        [=[#include "Utils.h"
#include "ThunderDeterministic.h" // Thunder patch (see cmake/CloudSeed.cmake)]=])

    _thunder_cloudseed_replace(${_file}
        [=[std::rand() / (float)RAND_MAX]=]
        [=[Cloudseed::ThunderPhaseRandom()]=])

    # Remember the starting phase, and return to it when the buffers are cleared.
    _thunder_cloudseed_replace(${_file}
        [=[float modPhase;]=]
        [=[float modPhase;
		float thunderStartPhase = 0; // Thunder patch (see cmake/CloudSeed.cmake)]=])

    _thunder_cloudseed_replace(${_file}
        [=[modPhase = 0.01 + 0.98 * ]=]
        [=[modPhase = thunderStartPhase = 0.01 + 0.98 * ]=])

    _thunder_cloudseed_replace(${_file}
        [=[Utils::ZeroBuffer(delayBuffer, DelayBufferSize);]=]
        [=[// Thunder patch (see cmake/CloudSeed.cmake): skippable zeroing, and the
			// modulation back to its start, updated on the next sample.
			if (!Cloudseed::ThunderSkipDelayZeroing())
				Utils::ZeroBuffer(delayBuffer, DelayBufferSize);
			modPhase = thunderStartPhase;
			samplesProcessed = ModulationUpdateRate;]=])

    _thunder_cloudseed_write(${_file})
endforeach()

# ---- DSP/MultitapDelay.h: skippable zeroing ------------------------------------------------------------------------------
file(READ "${_cloudseed_source}/DSP/MultitapDelay.h" _patch_text)

_thunder_cloudseed_replace(DSP/MultitapDelay.h
    [=[#include "Utils.h"]=]
    [=[#include "Utils.h"
#include "ThunderDeterministic.h" // Thunder patch (see cmake/CloudSeed.cmake)]=])

_thunder_cloudseed_replace(DSP/MultitapDelay.h
    [=[Utils::ZeroBuffer(delayBuffer, DelayBufferSize);]=]
    [=[if (!Cloudseed::ThunderSkipDelayZeroing()) // Thunder patch (see cmake/CloudSeed.cmake)
				Utils::ZeroBuffer(delayBuffer, DelayBufferSize);]=])

_thunder_cloudseed_write(DSP/MultitapDelay.h)

foreach(_file IN LISTS _cloudseed_patched_files)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_cloudseed_source}/${_file}")
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/compat/CloudSeedDeterministic.h")

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
