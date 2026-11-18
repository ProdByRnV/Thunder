#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace thunder
{

// Builds every host parameter from the table in core/params/ParamDefs.h, in
// groups (Output, Key, Pitch, ...) that hosts and the generic editor show as
// sections.
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

} // namespace thunder
