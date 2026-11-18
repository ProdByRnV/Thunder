#include "ParameterLayout.h"

#include "core/params/ParamDefs.h"
#include "core/params/ParamText.h"

namespace thunder
{

namespace
{
using namespace params;

juce::String toJuceString (std::string_view text)
{
    return juce::String::fromUTF8 (text.data(), static_cast<int> (text.size()));
}

std::unique_ptr<juce::RangedAudioParameter> createParameter (const ParamDef& def)
{
    const juce::ParameterID id{toJuceString (def.id), def.versionHint};
    const auto name = toJuceString (def.name);

    // `def` refers into the constexpr table, which outlives every parameter.
    const auto* d = &def;
    auto toTextFn = [d] (float value, int /*maximumLength*/) { return toText (*d, value); };
    auto fromTextFn = [d] (const juce::String& text) { return fromText (*d, text).value_or (d->def); };

    switch (def.kind)
    {
        case Kind::Float:
            return std::make_unique<juce::AudioParameterFloat> (id, name, makeRange (def), def.def,
                                                                juce::AudioParameterFloatAttributes()
                                                                    .withStringFromValueFunction (toTextFn)
                                                                    .withValueFromStringFunction (fromTextFn)
                                                                    .withAutomatable (def.automatable));

        case Kind::Int:
            return std::make_unique<juce::AudioParameterInt> (
                id, name, juce::roundToInt (def.min), juce::roundToInt (def.max), juce::roundToInt (def.def),
                juce::AudioParameterIntAttributes()
                    .withStringFromValueFunction ([d] (int value, int)
                                                  { return toText (*d, static_cast<float> (value)); })
                    .withValueFromStringFunction ([d] (const juce::String& text)
                                                  { return juce::roundToInt (fromText (*d, text).value_or (d->def)); })
                    .withAutomatable (def.automatable));

        case Kind::Bool:
            return std::make_unique<juce::AudioParameterBool> (
                id, name, def.def >= 0.5f, juce::AudioParameterBoolAttributes().withAutomatable (def.automatable));

        case Kind::Choice:
        {
            juce::StringArray choices;
            for (const auto choice : def.choices)
                choices.add (toJuceString (choice));

            return std::make_unique<juce::AudioParameterChoice> (
                id, name, choices, juce::roundToInt (def.def),
                juce::AudioParameterChoiceAttributes().withAutomatable (def.automatable));
        }
    }

    jassertfalse;
    return nullptr;
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (const auto& group : kGroupDefs)
    {
        auto parameterGroup = std::make_unique<juce::AudioProcessorParameterGroup> (toJuceString (group.id),
                                                                                    toJuceString (group.name), "|");

        for (const auto& def : kParamDefs)
            if (def.group == group.group)
                parameterGroup->addChild (createParameter (def));

        layout.add (std::move (parameterGroup));
    }

    return layout;
}

} // namespace thunder
