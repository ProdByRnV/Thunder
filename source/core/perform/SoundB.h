#pragma once

#include "PerformanceStage.h"

#include <juce_data_structures/juce_data_structures.h>

#include <optional>

// Morph's sound B as the project keeps it (PRD FR-PLAY-002, §9.12
// `morphSoundB`): a name to show ("Night Static", "Current sound") and a
// complete set of sound values. It is saved inside the session as a preset
// document, so it is read back through the one validated preset reader with
// the same version handling and migrations as everything else (FR-PRESET-009):
//
//   <Session …>
//     <MorphB><ThunderPreset stateVersion="2" …><Meta name="Night Static"/><Params>…</Params></ThunderPreset></MorphB>
//   </Session>
//
// Never part of a preset. Message thread / non-real-time only.

namespace thunder::perform
{

struct SoundB
{
    juce::String name;
    std::array<float, params::kNumParams> values{};

    // Every value at its default (the starting point for a B from a preset).
    static SoundB defaults (const juce::String& name);

    // What the audio thread gets.
    MorphSound toMorphSound() const noexcept;

    // The Session child (type MorphB).
    juce::ValueTree toValueTree (const juce::String& thunderVersion) const;

    // nullopt if `tree` isn't a readable MorphB.
    static std::optional<SoundB> fromValueTree (const juce::ValueTree& tree);

    bool operator== (const SoundB& other) const noexcept;
};

} // namespace thunder::perform
