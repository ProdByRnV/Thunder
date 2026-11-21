#pragma once

#include "core/params/ParamDefs.h"

#include <juce_audio_processors/juce_audio_processors.h>

// What each control does, in a sentence (PRD UI-GEN-005, UI.md §9.14). The
// tooltips show it with the control's name, and screen readers read it as the
// control's description (UI-A11Y-001).

namespace thunder::ui::help
{

// The sentence for a parameter. Never empty.
juce::String describe (params::ParamId id);

// The tooltip for a parameter's control: name and description, then the
// current value and its range for numbers ("3.20 s · 0.20 s – 20.0 s").
juce::String tooltip (params::ParamId id, const juce::RangedAudioParameter& parameter);

// The name the UI shows for a parameter (PRD §9's name).
juce::String name (params::ParamId id);

} // namespace thunder::ui::help
