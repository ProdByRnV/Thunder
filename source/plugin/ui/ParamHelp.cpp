#include "ParamHelp.h"

namespace thunder::ui::help
{

using params::ParamId;

juce::String describe (ParamId id)
{
    switch (id)
    {
        // Output
        case ParamId::outputMode:
            return "Blend mixes the layer with your melody; Layer only plays the layer by itself.";
        case ParamId::mix:
            return "The balance between your melody and the layer, in Blend.";
        case ParamId::inputGain:
            return "The level going into Thunder.";
        case ParamId::layerLevel:
            return "The level of the layer before it is mixed.";
        case ParamId::outputGain:
            return "The level coming out of Thunder.";
        case ParamId::autoGain:
            return "Keeps the layer at a steady level under your melody, whatever the other settings.";
        case ParamId::safetyLimiter:
            return "Keeps the output from going over -0.3 dBFS.";

        // Key
        case ParamId::keyRoot:
            return "The key your melody is in. Interval mode stays in this key.";
        case ParamId::keyScale:
            return "The scale of the key your melody is in.";

        // Pitch
        case ParamId::pitchOn:
            return "Switches pitch shifting on or off.";
        case ParamId::pitchMode:
            return "Octave shifts by whole octaves, Interval adds an in-key interval to each note, Semitone shifts by "
                   "a fixed amount.";
        case ParamId::pitchOctave:
            return "How many octaves the layer moves up or down.";
        case ParamId::pitchInterval:
            return "The in-key interval added above or below each note.";
        case ParamId::pitchDirection:
            return "Whether the interval goes up or down.";
        case ParamId::pitchSemitones:
            return "A fixed shift in semitones.";
        case ParamId::pitchFine:
            return "Fine tuning in cents.";
        case ParamId::pitchFormant:
            return "Keeps voices sounding natural when the pitch goes up.";
        case ParamId::pitchMix:
            return "How much of the shifted sound the layer uses.";

        // Grains
        case ParamId::grainOn:
            return "Switches the grain cloud on or off.";
        case ParamId::grainAmount:
            return "How much of the layer is made of grains.";
        case ParamId::grainSize:
            return "The length of each grain.";
        case ParamId::grainDensity:
            return "How many grains start each second.";
        case ParamId::grainPosSpread:
            return "How far back in time grains can reach.";
        case ParamId::grainPitchSpread:
            return "How far each grain's pitch can wander.";
        case ParamId::grainPanSpread:
            return "How widely grains spread across the stereo field.";
        case ParamId::grainReverse:
            return "The chance that a grain plays backwards.";

        // Space
        case ParamId::spaceOn:
            return "Switches the reverb on or off.";
        case ParamId::spaceMode:
            return "The character of the reverb.";
        case ParamId::spaceMix:
            return "How much reverb the layer gets.";
        case ParamId::spaceDecay:
            return "How long the reverb rings.";
        case ParamId::spacePreDelay:
            return "The gap before the reverb starts.";
        case ParamId::spaceDamping:
            return "Darkens the reverb's tail.";

        // Tone
        case ParamId::toneOn:
            return "Switches the layer's filters on or off.";
        case ParamId::hpFreq:
            return "Removes the layer's low end below this frequency.";
        case ParamId::hpSlope:
            return "How steeply the high-pass cuts, in dB per octave.";
        case ParamId::lpFreq:
            return "Removes the layer's high end above this frequency.";
        case ParamId::lpSlope:
            return "How steeply the low-pass cuts, in dB per octave.";

        // Width
        case ParamId::widthOn:
            return "Switches stereo width on or off.";
        case ParamId::widthAmount:
            return "The layer's stereo width: 0% is mono, 100% unchanged, 200% extra wide.";
    }

    // Only an ID outside the table gets here (every parameter has a case above).
    return "A Thunder control.";
}

juce::String name (ParamId id)
{
    const auto& def = params::paramDef (id);
    return juce::String::fromUTF8 (def.name.data(), static_cast<int> (def.name.size()));
}

juce::String tooltip (ParamId id, const juce::RangedAudioParameter& parameter)
{
    const auto& def = params::paramDef (id);
    auto text = name (id) + ": " + describe (id);

    if (def.kind == params::Kind::Float || def.kind == params::Kind::Int)
        text << "\n"
             << parameter.getCurrentValueAsText() << "  (" << parameter.getText (0.0f, 32) << " to "
             << parameter.getText (1.0f, 32) << ")";

    return text;
}

} // namespace thunder::ui::help
