#pragma once

#include "core/params/ParamDefs.h"

#include <juce_core/juce_core.h>

#include <array>
#include <cstdint>
#include <optional>

// A preset file: what is in one, how it is written and how it is read back
// (PRD FR-PRESET-002/009, ARCHITECTURE §9.4). Factory, user and (later)
// imported presets all go through this one reader.
//
//   <ThunderPreset stateVersion="1" createdWith="0.5.0">
//     <Meta name="Night Static" author="Thunder" category="Atmos" tags="dark,wide"
//           description="Slow grains under a cloud reverb."/>
//     <Params>
//       <P id="grainAmount" v="62"/>
//       <P id="spaceMode" v="Cloud"/>      (choices by name, values in real units)
//     </Params>
//   </ThunderPreset>
//
// - The file is untrusted: the size is checked first, then the XML, the root,
//   the version and every value. A refused file changes nothing, and the reason
//   names no path and quotes no contents.
// - A preset becomes a parameters-only state tree and goes through the same
//   version check and migrations as a host session (core/state/StateManager),
//   so a preset is accepted on exactly the terms host state is.
// - A parameter the preset doesn't name takes its default; an unknown one is
//   ignored (and counted, so factory presets can be checked strictly).
// - Metadata is trimmed and length-limited when read and when written.
//
// Message thread / non-real-time only.

namespace thunder::presets
{

inline constexpr const char* kFileExtension = ".thunderpreset";
inline constexpr const char* kFileWildcard = "*.thunderpreset";

inline constexpr int kMaxFieldLength = 128;
inline constexpr int kMaxDescriptionLength = 1024;
inline constexpr std::int64_t kMaxFileBytes = 1024 * 1024; // a preset is a few KB

struct Metadata
{
    juce::String name;
    juce::String author;
    juce::String category;
    juce::String tags; // comma-separated
    juce::String description;

    bool operator== (const Metadata&) const = default;
};

// Whether presets carry a parameter: every sound control, but not Morph's
// amount, which only means something with the project's sound B (PRD
// FR-PLAY-002). A preset that names it anyway is read without it.
constexpr bool isPresetParameter (params::ParamId id) noexcept
{
    return id != params::ParamId::morphAmount;
}

// Every field trimmed and cut to its limit.
Metadata sanitise (const Metadata& metadata);

// A sound: values (real units) for some parameters; the rest are at their defaults.
struct Preset
{
    Metadata meta;
    std::array<std::optional<float>, params::kNumParams> values{};

    float valueOr (params::ParamId id) const noexcept;

    // Every parameter set to `current`: what a user save writes, so a later
    // change to a default can never change a saved sound.
    static Preset complete (const Metadata& meta, const std::array<float, params::kNumParams>& current);
};

enum class ReadResult
{
    ok,
    tooLarge,   // bigger than kMaxFileBytes
    unreadable, // the file couldn't be opened
    notAPreset, // not XML, or not a Thunder preset
    badVersion, // no usable stateVersion
    badValue    // a value that isn't a number (or a known yes/no)
};

struct ReadReport
{
    ReadResult result = ReadResult::ok;
    int unknownParameters = 0; // ids this version doesn't know (ignored)
    int unknownChoices = 0;    // choice names this version doesn't know (default used)
    bool fromNewerVersion = false;
    juce::String createdWith;

    bool ok() const noexcept { return result == ReadResult::ok; }
    bool isStrictlyValid() const noexcept { return ok() && unknownParameters == 0 && unknownChoices == 0; }
};

// The preset as file text.
juce::String write (const Preset& preset, const juce::String& thunderVersion);

// Validates, migrates and reads `text`. `out` is changed only when the result is ok.
ReadReport read (const juce::String& text, Preset& out);

// The same, from a file: the size is checked before anything is read.
ReadReport readFile (const juce::File& file, Preset& out);

// Why a preset couldn't be loaded, in words that name no path and quote no contents.
juce::String describe (ReadResult result);

} // namespace thunder::presets
