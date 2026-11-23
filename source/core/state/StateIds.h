#pragma once

#include <juce_core/juce_core.h>

// Names used in Thunder's saved state (ARCHITECTURE §9.2). They are part of the
// saved-file format: never rename one; add new ones instead.

namespace thunder::state::ids
{

inline const juce::Identifier root{"ThunderState"};
inline const juce::Identifier stateVersion{"stateVersion"};
inline const juce::Identifier pluginVersion{"pluginVersion"};

// Host parameters, as written by juce::AudioProcessorValueTreeState: one
// <PARAM id="…" value="…"/> per parameter, values in real units.
inline const juce::Identifier parameters{"PARAMETERS"};
inline const juce::Identifier parameter{"PARAM"};
inline const juce::Identifier parameterId{"id"};
inline const juce::Identifier parameterValue{"value"};

inline const juce::Identifier session{"Session"};
inline const juce::Identifier presetName{"presetName"};
inline const juce::Identifier presetModified{"presetModified"};
inline const juce::Identifier presetKey{"presetKey"};
inline const juce::Identifier captureBars{"captureBars"};
inline const juce::Identifier captureSource{"captureSource"};
inline const juce::Identifier uiScale{"uiScale"};
inline const juce::Identifier uiView{"uiView"};
inline const juce::Identifier quality{"quality"};
inline const juce::Identifier captureTail{"captureTail"};
inline const juce::Identifier captureLoop{"captureLoop"};
inline const juce::Identifier keyAutoDetect{"keyAutoDetect"};

// The session's source (PRD §9.12, FR-INPUT-004): a child of Session.
inline const juce::Identifier source{"Source"};
inline const juce::Identifier sourcePath{"path"};
inline const juce::Identifier sourceGridOffset{"gridOffsetSamples"};
inline const juce::Identifier sourceCaptured{"captured"};
inline const juce::Identifier sourceBpm{"bpm"};
inline const juce::Identifier sourceBars{"bars"};
inline const juce::Identifier sourceTimeSigNumerator{"timeSigNumerator"};
inline const juce::Identifier sourceTimeSigDenominator{"timeSigDenominator"};
inline const juce::Identifier sourceEmbedded{"embedded"}; // FLAC, base64

} // namespace thunder::state::ids
