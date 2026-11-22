#include "ParamText.h"

#include <algorithm>
#include <cmath>

namespace thunder::params
{

namespace
{
juce::String toJuceString (std::string_view text)
{
    return juce::String::fromUTF8 (text.data(), static_cast<int> (text.size()));
}

// "+3", "-3", "0": signed, with zero shown unsigned.
juce::String signedInt (int value)
{
    return value > 0 ? "+" + juce::String (value) : juce::String (value);
}

juce::String signedDecimal (float value, int decimals)
{
    const auto text = juce::String (value, decimals);

    // Rounding may leave "-0.0"; show it as "0.0".
    if (text.removeCharacters ("-0.").isEmpty())
        return juce::String (0.0f, decimals);

    return value > 0.0f ? "+" + text : text;
}

juce::String formatFloat (const ParamDef& def, float value)
{
    switch (def.unit)
    {
        case Unit::Decibels:
            return signedDecimal (value, 1) + " dB";

        case Unit::Percent:
            return juce::String (juce::roundToInt (value)) + "%";

        case Unit::Hertz:
            // Modulation rates go well below 1 Hz; audio frequencies start at 20.
            if (value < 19.95f)
                return juce::String (value, value < 1.0f ? 2 : 1) + " Hz";
            if (juce::roundToInt (value) < 1000)
                return juce::String (juce::roundToInt (value)) + " Hz";
            return juce::String (value / 1000.0f, value < 10000.0f ? 2 : 1) + " kHz";

        case Unit::Milliseconds:
            if (juce::roundToInt (value) < 1000)
                return juce::String (juce::roundToInt (value)) + " ms";
            return juce::String (value / 1000.0f, 2) + " s";

        case Unit::Semitones:
            if (def.min < 0.0f)
                return signedDecimal (value, 1) + " st";
            return juce::String (value, 1) + " st";

        case Unit::Cents:
            return signedInt (juce::roundToInt (value)) + " cents";

        case Unit::GrainsPerSecond:
            return juce::String (value, value < 10.0f ? 1 : 0) + " grains/s";

        case Unit::None:
            break;
    }

    return juce::String (value, 2);
}

// The leading number in `text` (after any spaces), if there is one.
std::optional<float> leadingNumber (const juce::String& text)
{
    const auto number = text.initialSectionContainingOnly ("+-.0123456789");

    if (!number.containsAnyOf ("0123456789"))
        return std::nullopt;

    return number.getFloatValue();
}
} // namespace

juce::String toText (const ParamDef& def, float value)
{
    switch (def.kind)
    {
        case Kind::Bool:
            return value >= 0.5f ? "On" : "Off";

        case Kind::Choice:
        {
            const auto index = std::clamp (juce::roundToInt (value), 0, std::max (def.numChoices() - 1, 0));
            return def.choices.empty() ? juce::String() : toJuceString (def.choices[static_cast<std::size_t> (index)]);
        }

        case Kind::Int:
        {
            const auto n = juce::roundToInt (value);
            switch (def.unit)
            {
                case Unit::Semitones:
                    return signedInt (n) + " st";
                case Unit::Cents:
                    return signedInt (n) + " cents";
                case Unit::None:
                case Unit::Percent:
                case Unit::Decibels:
                case Unit::Hertz:
                case Unit::Milliseconds:
                case Unit::GrainsPerSecond:
                    break;
            }
            return juce::String (n);
        }

        case Kind::Float:
            break;
    }

    return formatFloat (def, value);
}

std::optional<float> fromText (const ParamDef& def, const juce::String& rawText)
{
    // Accept a typographic minus (U+2212) as well as '-'.
    const auto text = rawText.replace (juce::CharPointer_UTF8 ("\xe2\x88\x92"), "-").trim().toLowerCase();

    if (text.isEmpty())
        return std::nullopt;

    const auto clampToRange = [&def] (float v) { return std::clamp (v, def.min, def.max); };

    switch (def.kind)
    {
        case Kind::Bool:
            if (text == "on" || text == "true" || text == "yes")
                return 1.0f;
            if (text == "off" || text == "false" || text == "no")
                return 0.0f;
            if (const auto n = leadingNumber (text))
                return *n >= 0.5f ? 1.0f : 0.0f;
            return std::nullopt;

        case Kind::Choice:
            for (std::size_t i = 0; i < def.choices.size(); ++i)
                if (text == toJuceString (def.choices[i]).toLowerCase())
                    return static_cast<float> (i);
            if (const auto n = leadingNumber (text))
                return clampToRange (std::round (*n));
            return std::nullopt;

        case Kind::Int:
            if (const auto n = leadingNumber (text))
                return clampToRange (std::round (*n));
            return std::nullopt;

        case Kind::Float:
            break;
    }

    auto number = leadingNumber (text);
    if (!number)
        return std::nullopt;

    auto value = *number;
    const auto unitText = text.substring (text.initialSectionContainingOnly ("+-.0123456789").length()).trim();

    switch (def.unit)
    {
        case Unit::Hertz:
            if (unitText.startsWith ("k"))
                value *= 1000.0f;
            break;

        case Unit::Milliseconds:
            if (unitText.startsWith ("s"))
                value *= 1000.0f;
            break;

        case Unit::None:
        case Unit::Percent:
        case Unit::Decibels:
        case Unit::Semitones:
        case Unit::Cents:
        case Unit::GrainsPerSecond:
            break;
    }

    return clampToRange (value);
}

juce::NormalisableRange<float> makeRange (const ParamDef& def)
{
    if (def.kind != Kind::Float)
        return {def.min, def.max, 1.0f};

    switch (def.taper)
    {
        case Taper::Log:
            // value = min · (max/min)^t, so each doubling of the value moves the
            // control by the same amount (the way frequency and time are heard).
            return {def.min, def.max,
                    [] (float start, float end, float t) { return start * std::pow (end / start, t); },
                    [] (float start, float end, float v) { return std::log (v / start) / std::log (end / start); },
                    [] (float start, float end, float v) { return std::clamp (v, start, end); }};

        case Taper::Linear:
            break;
    }

    return {def.min, def.max};
}

} // namespace thunder::params
