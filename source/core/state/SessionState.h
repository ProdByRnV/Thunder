#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace thunder::state
{

enum class CaptureSource
{
    Layer,
    FullMix
};

// Per-session settings that are saved with the host session but are not host
// parameters (PRD §9.12). Fields are added here as their features arrive.
//
// Message thread / non-real-time only: the audio thread never reads this.
struct SessionState
{
    juce::String presetName;
    bool presetModified = false;
    juce::String presetKey; // the loaded preset in the library (PRD FR-PRESET-014); empty = Init
    int captureBars = 4;    // 1, 2, 4, 8 or 16
    CaptureSource captureSource = CaptureSource::Layer;

    juce::ValueTree toValueTree() const;

    // Missing or invalid values fall back to the defaults above, so a damaged
    // or older session still loads.
    static SessionState fromValueTree (const juce::ValueTree& tree);

    bool operator== (const SessionState&) const = default;
};

} // namespace thunder::state
