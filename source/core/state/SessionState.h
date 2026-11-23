#pragma once

#include "core/params/Enums.h"

#include <juce_data_structures/juce_data_structures.h>

namespace thunder::state
{

enum class CaptureSource
{
    Layer,
    FullMix,
    Input // Thunder's input: becomes the source (PRD FR-INPUT-003)
};

// The visualiser's view in this session (UI.md §8.2); not set = the global default.
enum class VisualiserView
{
    notSet,
    wave,
    spectrum
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
    bool captureTail = false;  // Include Tail (PRD FR-CAPT-004)
    bool captureLoop = false;  // Loop: the tail folded onto the start
    bool keyAutoDetect = true; // PRD §9.2: detect the key of the input (never applied without a click)
    float uiScale = 0.0f;      // the editor size this session last used (UI.md §10.3); 0 = not set
    VisualiserView uiView = VisualiserView::notSet;
    params::Quality quality = params::Quality::Standard; // PRD FR-CORE-013; changes the latency

    juce::ValueTree toValueTree() const;

    // Missing or invalid values fall back to the defaults above, so a damaged
    // or older session still loads.
    static SessionState fromValueTree (const juce::ValueTree& tree);

    bool operator== (const SessionState& other) const noexcept
    {
        return presetName == other.presetName && presetModified == other.presetModified &&
               presetKey == other.presetKey && captureBars == other.captureBars &&
               captureSource == other.captureSource && juce::exactlyEqual (uiScale, other.uiScale) &&
               uiView == other.uiView && quality == other.quality && captureTail == other.captureTail &&
               captureLoop == other.captureLoop && keyAutoDetect == other.keyAutoDetect;
    }
};

} // namespace thunder::state
