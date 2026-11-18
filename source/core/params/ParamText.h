#pragma once

#include "ParamDefs.h"

#include <juce_core/juce_core.h>

#include <optional>

// Turning parameter values into text and back (UI.md §4.3), and the mapping of
// each parameter onto a host's 0..1 range. Used by the host parameters and,
// later, by Thunder's own UI and accessibility text.
//
// Host-facing text uses ASCII only ("-12.0 dB", not a typographic minus), so it
// reads correctly in every host.

namespace thunder::params
{

// [MSG] e.g. "+3.5 dB", "45%", "1.25 kHz", "80 ms", "+7 st", "Natural Minor".
juce::String toText (const ParamDef& def, float value);

// [MSG] Parses what a user typed into a host's value field. Accepts the unit or
// not ("200", "200 Hz", "1.5k", "1.5 kHz"; "1.5 s" for times), a typographic
// minus, and for choices the option name or its number. The result is clamped
// to the parameter's range; nullopt if no number or option was found.
std::optional<float> fromText (const ParamDef& def, const juce::String& text);

// The 0..1 ↔ real-units mapping for a parameter (linear, or logarithmic for
// Taper::Log). Integer, bool and choice parameters snap to whole steps.
juce::NormalisableRange<float> makeRange (const ParamDef& def);

} // namespace thunder::params
