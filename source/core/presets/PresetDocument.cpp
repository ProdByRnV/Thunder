#include "PresetDocument.h"

#include "core/state/StateIds.h"
#include "core/state/StateManager.h"

#include <cmath>
#include <cstdlib>

namespace thunder::presets
{

namespace
{
const juce::Identifier kRoot{"ThunderPreset"};
const juce::Identifier kMeta{"Meta"};
const juce::Identifier kParams{"Params"};
const juce::Identifier kParam{"P"};
const juce::Identifier kId{"id"};
const juce::Identifier kValue{"v"};
const juce::Identifier kCreatedWith{"createdWith"};

// The element and attribute names an APVTS state uses (juce::AudioProcessorValueTreeState).
const juce::Identifier kStateParam{"PARAM"};
const juce::Identifier kStateValue{"value"};

constexpr const char* kMetaFields[] = {"name", "author", "category", "tags", "description"};

juce::String toJuce (std::string_view text)
{
    return juce::String::fromUTF8 (text.data(), static_cast<int> (text.size()));
}

juce::String bounded (const juce::String& text, int maximum)
{
    const auto trimmed = text.trim();
    return trimmed.length() > maximum ? trimmed.substring (0, maximum).trimEnd() : trimmed;
}

juce::String valueText (const params::ParamDef& def, float value)
{
    switch (def.kind)
    {
        case params::Kind::Choice:
        {
            const auto index = juce::jlimit (0, def.numChoices() - 1, juce::roundToInt (value));
            return toJuce (def.choices[static_cast<std::size_t> (index)]);
        }
        case params::Kind::Bool:
            return value >= 0.5f ? "1" : "0";
        case params::Kind::Int:
            return juce::String (juce::roundToInt (value));
        case params::Kind::Float:
            break;
    }

    return juce::String::formatted ("%.7g", static_cast<double> (value));
}

// A plain decimal number, or nothing.
std::optional<double> parseNumber (const juce::String& text)
{
    const auto t = text.trim();
    if (t.isEmpty() || !t.containsOnly ("0123456789.-+eE"))
        return std::nullopt;

    const auto utf8 = t.toStdString();
    char* end = nullptr;
    const auto value = std::strtod (utf8.c_str(), &end);
    if (end != utf8.c_str() + utf8.size() || !std::isfinite (value))
        return std::nullopt;

    return value;
}

ReadReport refused (ReadResult result)
{
    ReadReport report;
    report.result = result;
    return report;
}

enum class Parsed
{
    ok,
    unknownChoice,
    bad
};

Parsed parseValue (const params::ParamDef& def, const juce::String& text, float& out)
{
    switch (def.kind)
    {
        case params::Kind::Choice:
            for (int i = 0; i < def.numChoices(); ++i)
                if (text == toJuce (def.choices[static_cast<std::size_t> (i)]))
                {
                    out = static_cast<float> (i);
                    return Parsed::ok;
                }
            return Parsed::unknownChoice;

        case params::Kind::Bool:
        {
            const auto t = text.trim().toLowerCase();
            if (t == "1" || t == "true" || t == "on")
                out = 1.0f;
            else if (t == "0" || t == "false" || t == "off")
                out = 0.0f;
            else
                return Parsed::bad;
            return Parsed::ok;
        }

        case params::Kind::Int:
        case params::Kind::Float:
        {
            const auto number = parseNumber (text);
            if (!number)
                return Parsed::bad;

            auto v = static_cast<float> (
                juce::jlimit (static_cast<double> (def.min), static_cast<double> (def.max), *number));
            if (def.kind == params::Kind::Int)
                v = static_cast<float> (juce::roundToInt (v));
            out = v;
            return Parsed::ok;
        }
    }

    return Parsed::bad;
}
} // namespace

Metadata sanitise (const Metadata& m)
{
    Metadata s;
    s.name = bounded (m.name, kMaxFieldLength);
    s.author = bounded (m.author, kMaxFieldLength);
    s.category = bounded (m.category, kMaxFieldLength);
    s.tags = bounded (m.tags, kMaxFieldLength);
    s.description = bounded (m.description, kMaxDescriptionLength);
    return s;
}

float Preset::valueOr (params::ParamId id) const noexcept
{
    return values[params::toIndex (id)].value_or (params::paramDef (id).def);
}

Preset Preset::complete (const Metadata& meta, const std::array<float, params::kNumParams>& current)
{
    Preset p;
    p.meta = sanitise (meta);
    for (std::size_t i = 0; i < params::kNumParams; ++i)
        p.values[i] = current[i];
    return p;
}

juce::String write (const Preset& preset, const juce::String& thunderVersion)
{
    juce::XmlElement root (kRoot.toString());
    root.setAttribute (state::ids::stateVersion, state::kCurrentStateVersion);
    root.setAttribute (kCreatedWith, thunderVersion);

    const auto meta = sanitise (preset.meta);
    const juce::String* fields[] = {&meta.name, &meta.author, &meta.category, &meta.tags, &meta.description};
    auto* metaElement = root.createNewChildElement (kMeta.toString());
    for (std::size_t i = 0; i < std::size (kMetaFields); ++i)
        if (fields[i]->isNotEmpty())
            metaElement->setAttribute (kMetaFields[i], *fields[i]);

    auto* paramsElement = root.createNewChildElement (kParams.toString());
    for (const auto& def : params::kParamDefs)
        if (const auto& v = preset.values[params::toIndex (def.param)])
        {
            auto* p = paramsElement->createNewChildElement (kParam.toString());
            p->setAttribute (kId, toJuce (def.id));
            p->setAttribute (kValue, valueText (def, *v));
        }

    juce::XmlElement::TextFormat format;
    format.lineWrapLength = 100;
    return root.toString (format);
}

ReadReport read (const juce::String& text, Preset& out)
{
    ReadReport report;

    if (static_cast<std::int64_t> (text.getNumBytesAsUTF8()) > kMaxFileBytes)
        return refused (ReadResult::tooLarge);

    const auto xml = juce::parseXML (text);
    if (xml == nullptr || !xml->hasTagName (kRoot.toString()))
        return refused (ReadResult::notAPreset);

    // The version decides which migrations apply; without one the file can't
    // be read safely.
    const auto versionText = xml->getStringAttribute (state::ids::stateVersion).trim();
    if (versionText.isEmpty() || !versionText.containsOnly ("0123456789") || versionText.length() > 6)
        return refused (ReadResult::badVersion);

    const auto version = versionText.getIntValue();
    if (version < 1)
        return refused (ReadResult::badVersion);

    report.createdWith = bounded (xml->getStringAttribute (kCreatedWith), kMaxFieldLength);

    Preset preset;
    if (const auto* meta = xml->getChildByName (kMeta.toString()))
    {
        Metadata m;
        juce::String* fields[] = {&m.name, &m.author, &m.category, &m.tags, &m.description};
        for (std::size_t i = 0; i < std::size (kMetaFields); ++i)
            *fields[i] = meta->getStringAttribute (kMetaFields[i]);
        preset.meta = sanitise (m);
    }

    // The values, as a parameters-only state tree of this version.
    juce::ValueTree parameters{state::ids::parameters};
    if (const auto* list = xml->getChildByName (kParams.toString()))
    {
        for (const auto* p : list->getChildWithTagNameIterator (kParam.toString()))
        {
            const auto id = p->getStringAttribute (kId);
            const auto param = params::findParam (id.toStdString());
            if (!param)
            {
                ++report.unknownParameters; // a newer or misspelled parameter: ignored
                continue;
            }

            if (!p->hasAttribute (kValue))
                return refused (ReadResult::badValue);

            float value = 0.0f;
            switch (parseValue (params::paramDef (*param), p->getStringAttribute (kValue), value))
            {
                case Parsed::ok:
                    break;
                case Parsed::unknownChoice:
                    ++report.unknownChoices;
                    continue;
                case Parsed::bad:
                    return refused (ReadResult::badValue);
            }

            juce::ValueTree entry{kStateParam};
            entry.setProperty (kId, id, nullptr);
            entry.setProperty (kStateValue, value, nullptr);
            parameters.appendChild (entry, nullptr);
        }
    }

    juce::ValueTree stateTree{state::ids::root};
    stateTree.setProperty (state::ids::stateVersion, version, nullptr);
    stateTree.appendChild (parameters, nullptr);

    // The same version handling and migrations as a host session.
    const auto loaded = state::loadState (stateTree);
    if (!loaded)
        return refused (ReadResult::notAPreset);

    report.fromNewerVersion = loaded->fromNewerVersion;

    for (const auto& entry : loaded->parameters)
    {
        const auto param = params::findParam (entry.getProperty (kId).toString().toStdString());
        if (!param)
            continue;

        const auto& def = params::paramDef (*param);
        const auto value = static_cast<float> (static_cast<double> (entry.getProperty (kStateValue)));
        if (std::isfinite (value))
            preset.values[params::toIndex (*param)] = juce::jlimit (def.min, def.max, value);
    }

    out = std::move (preset);
    return report;
}

ReadReport readFile (const juce::File& file, Preset& out)
{
    if (!file.existsAsFile())
        return refused (ReadResult::unreadable);

    if (file.getSize() > kMaxFileBytes)
        return refused (ReadResult::tooLarge);

    juce::FileInputStream stream (file);
    if (!stream.openedOk())
        return refused (ReadResult::unreadable);

    return read (stream.readEntireStreamAsString(), out);
}

juce::String describe (ReadResult result)
{
    switch (result)
    {
        case ReadResult::ok:
            return {};
        case ReadResult::tooLarge:
        case ReadResult::notAPreset:
            return "That file isn't a Thunder preset. Your sound wasn't changed.";
        case ReadResult::unreadable:
            return "That preset couldn't be opened. Your sound wasn't changed.";
        case ReadResult::badVersion:
        case ReadResult::badValue:
            return "That preset is damaged. Your sound wasn't changed.";
    }

    return "That preset couldn't be loaded. Your sound wasn't changed.";
}

} // namespace thunder::presets
