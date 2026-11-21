#pragma once

#include "core/params/ParamDefs.h"

#include <juce_audio_processors/juce_audio_processors.h>

// Small helpers for connecting UI controls to host parameters by their
// ParamId (ARCHITECTURE §12.1: controls only ever talk to parameters).

namespace thunder::ui
{

inline juce::String paramIdString (params::ParamId id)
{
    const auto& def = params::paramDef (id);
    return juce::String::fromUTF8 (def.id.data(), static_cast<int> (def.id.size()));
}

inline juce::RangedAudioParameter& parameter (juce::AudioProcessorValueTreeState& apvts, params::ParamId id)
{
    auto* p = apvts.getParameter (paramIdString (id));
    jassert (p != nullptr);
    return *p;
}

// Calls `onChange` with the parameter's value (real units) on the message
// thread now and whenever it changes, from the UI, the host or a preset.
class ParameterWatcher
{
public:
    ParameterWatcher (juce::AudioProcessorValueTreeState& apvts, params::ParamId id,
                      std::function<void (float)> onChange)
        : attachment (parameter (apvts, id), std::move (onChange), nullptr)
    {
        attachment.sendInitialUpdate();
    }

private:
    juce::ParameterAttachment attachment;
};

} // namespace thunder::ui
