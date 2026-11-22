#pragma once

#include <juce_core/juce_core.h>

// Names used in Thunder's saved state (ARCHITECTURE §9.2). They are part of the
// saved-file format: never rename one; add new ones instead.

namespace thunder::state::ids
{

inline const juce::Identifier root{"ThunderState"};
inline const juce::Identifier stateVersion{"stateVersion"};
inline const juce::Identifier pluginVersion{"pluginVersion"};

// Host parameters, as written by juce::AudioProcessorValueTreeState.
inline const juce::Identifier parameters{"PARAMETERS"};

inline const juce::Identifier session{"Session"};
inline const juce::Identifier presetName{"presetName"};
inline const juce::Identifier presetModified{"presetModified"};
inline const juce::Identifier presetKey{"presetKey"};
inline const juce::Identifier captureBars{"captureBars"};
inline const juce::Identifier captureSource{"captureSource"};
inline const juce::Identifier uiScale{"uiScale"};
inline const juce::Identifier uiView{"uiView"};

} // namespace thunder::state::ids
