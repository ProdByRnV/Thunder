#include "SoundB.h"

#include "core/presets/PresetDocument.h"
#include "core/state/StateIds.h"

namespace thunder::perform
{

SoundB SoundB::defaults (const juce::String& name)
{
    SoundB b;
    b.name = name;
    for (const auto& def : params::kParamDefs)
        b.values[params::toIndex (def.param)] = def.def;
    return b;
}

MorphSound SoundB::toMorphSound() const noexcept
{
    MorphSound sound;
    sound.present = true;
    sound.values = values;
    return sound;
}

juce::ValueTree SoundB::toValueTree (const juce::String& thunderVersion) const
{
    presets::Preset preset;
    preset.meta.name = name;
    for (const auto& def : params::kParamDefs)
        if (isMorphed (def.param))
            preset.values[params::toIndex (def.param)] = values[params::toIndex (def.param)];

    juce::ValueTree tree{state::ids::morphSoundB};
    if (const auto xml = juce::parseXML (presets::write (preset, thunderVersion)))
        tree.appendChild (juce::ValueTree::fromXml (*xml), nullptr);
    return tree;
}

std::optional<SoundB> SoundB::fromValueTree (const juce::ValueTree& tree)
{
    if (!tree.hasType (state::ids::morphSoundB) || tree.getNumChildren() != 1)
        return std::nullopt;

    const auto xml = tree.getChild (0).createXml();
    if (xml == nullptr)
        return std::nullopt;

    presets::Preset preset;
    if (!presets::read (xml->toString(), preset).ok())
        return std::nullopt;

    auto b = defaults (preset.meta.name);
    for (const auto& def : params::kParamDefs)
        b.values[params::toIndex (def.param)] = preset.valueOr (def.param);
    return b;
}

bool SoundB::operator== (const SoundB& other) const noexcept
{
    if (name != other.name)
        return false;

    for (const auto& def : params::kParamDefs)
        if (isMorphed (def.param) &&
            !juce::exactlyEqual (values[params::toIndex (def.param)], other.values[params::toIndex (def.param)]))
            return false;

    return true;
}

} // namespace thunder::perform
