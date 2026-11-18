// The parameter table (PRD §9), value formatting (UI.md §4.3) and the
// per-block parameter snapshot.

#include "core/params/ParamDefs.h"
#include "core/params/ParamSnapshot.h"
#include "core/params/ParamText.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cctype>
#include <cmath>
#include <string>

using namespace thunder::params;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace
{
juce::String text (ParamId id, float value)
{
    return toText (paramDef (id), value);
}

std::optional<float> parse (ParamId id, const juce::String& typed)
{
    return fromText (paramDef (id), typed);
}

bool parsesTo (ParamId id, const juce::String& typed, float expected)
{
    const auto value = parse (id, typed);
    return value.has_value() && std::abs (*value - expected) < 1.0e-4f;
}
} // namespace

TEST_CASE ("The parameter table holds every v0.5 parameter", "[unit][params]")
{
    CHECK (kNumParams == 39u);

    for (const auto& def : kParamDefs)
    {
        INFO (std::string (def.id));

        // IDs are plain identifiers: hosts and preset files store them.
        for (const auto c : def.id)
            CHECK (std::isalnum (static_cast<unsigned char> (c)));

        CHECK (findParam (def.id) == def.param);
        CHECK (def.versionHint == kReleaseV0_5);
        CHECK (def.automatable);

        if (def.kind == Kind::Choice)
            CHECK (def.numChoices() >= 2);
        else
            CHECK (def.choices.empty());
    }

    CHECK_FALSE (findParam ("noSuchParameter").has_value());
}

TEST_CASE ("Parameter defaults follow the PRD", "[unit][params]")
{
    const auto defaults = ParamSnapshot::defaults();

    CHECK (defaults.getChoice<OutputMode> (ParamId::outputMode) == OutputMode::Blend);
    CHECK_THAT (defaults.get (ParamId::mix), WithinAbs (50.0, 1e-6));
    CHECK (defaults.isOn (ParamId::autoGain));
    CHECK (defaults.isOn (ParamId::safetyLimiter));
    CHECK (defaults.getChoice<KeyRoot> (ParamId::keyRoot) == KeyRoot::C);
    CHECK (defaults.getChoice<Scale> (ParamId::keyScale) == Scale::NaturalMinor);
    CHECK (defaults.getChoice<PitchMode> (ParamId::pitchMode) == PitchMode::Octave);
    CHECK (defaults.getChoice<PitchOctave> (ParamId::pitchOctave) == PitchOctave::Up1);
    CHECK (defaults.getChoice<PitchInterval> (ParamId::pitchInterval) == PitchInterval::Third);
    CHECK (defaults.getInt (ParamId::pitchSemitones) == 0);
    CHECK_THAT (defaults.get (ParamId::grainSize), WithinAbs (80.0, 1e-6));
    CHECK (defaults.getChoice<SpaceMode> (ParamId::spaceMode) == SpaceMode::Cloud);
    CHECK_THAT (defaults.get (ParamId::hpFreq), WithinAbs (200.0, 1e-6));
    CHECK (defaults.getChoice<FilterSlope> (ParamId::hpSlope) == FilterSlope::Db24);
    CHECK_THAT (defaults.get (ParamId::lpFreq), WithinAbs (16000.0, 1e-6));
    CHECK (defaults.getChoice<FilterSlope> (ParamId::lpSlope) == FilterSlope::Db12);
    CHECK_THAT (defaults.get (ParamId::widthAmount), WithinAbs (100.0, 1e-6));
}

TEST_CASE ("The snapshot copies live values and keeps choices in range", "[unit][params]")
{
    std::array<std::atomic<float>, kNumParams> live;
    ParamRefs refs{};
    for (std::size_t i = 0; i < kNumParams; ++i)
    {
        live[i].store (kParamDefs[i].def);
        refs[i] = &live[i];
    }

    live[toIndex (ParamId::mix)].store (75.0f);
    live[toIndex (ParamId::keyScale)].store (99.0f); // out of range
    live[toIndex (ParamId::pitchSemitones)].store (-6.6f);

    auto snapshot = ParamSnapshot::defaults();
    snapshot.readFrom (refs);

    CHECK_THAT (snapshot.get (ParamId::mix), WithinAbs (75.0, 1e-6));
    CHECK (snapshot.getChoice<Scale> (ParamId::keyScale) == Scale::Chromatic);
    CHECK (snapshot.getInt (ParamId::pitchSemitones) == -7);
}

TEST_CASE ("Values are displayed as UI.md specifies", "[unit][params]")
{
    CHECK (text (ParamId::inputGain, 3.5f) == "+3.5 dB");
    CHECK (text (ParamId::inputGain, -12.0f) == "-12.0 dB");
    CHECK (text (ParamId::inputGain, 0.0f) == "0.0 dB");
    CHECK (text (ParamId::inputGain, -0.04f) == "0.0 dB");

    CHECK (text (ParamId::mix, 45.2f) == "45%");

    CHECK (text (ParamId::hpFreq, 200.0f) == "200 Hz");
    CHECK (text (ParamId::lpFreq, 16000.0f) == "16.0 kHz");
    CHECK (text (ParamId::hpFreq, 1250.0f) == "1.25 kHz");

    CHECK (text (ParamId::grainSize, 80.0f) == "80 ms");
    const auto longTime =
        detail::floatParam (ParamId::grainSize, "t", "t", Group::Grains, 0.0f, 5000.0f, 0.0f, Unit::Milliseconds);
    CHECK (toText (longTime, 1500.0f) == "1.50 s");

    CHECK (text (ParamId::pitchSemitones, 7.0f) == "+7 st");
    CHECK (text (ParamId::pitchSemitones, -12.0f) == "-12 st");
    CHECK (text (ParamId::pitchSemitones, 0.0f) == "0 st");
    CHECK (text (ParamId::pitchFine, -12.0f) == "-12 cents");
    CHECK (text (ParamId::grainPitchSpread, 3.5f) == "3.5 st");

    CHECK (text (ParamId::grainDensity, 20.0f) == "20 grains/s");
    CHECK (text (ParamId::grainDensity, 2.5f) == "2.5 grains/s");

    CHECK (text (ParamId::keyScale, 1.0f) == "Natural Minor");
    CHECK (text (ParamId::pitchOctave, 0.0f) == "-2");
    CHECK (text (ParamId::hpSlope, 1.0f) == "24 dB/oct");
    CHECK (text (ParamId::pitchOn, 1.0f) == "On");
    CHECK (text (ParamId::pitchOn, 0.0f) == "Off");
}

TEST_CASE ("Typed values are understood", "[unit][params]")
{
    CHECK (parsesTo (ParamId::hpFreq, "1.5k", 1500.0f));
    CHECK (parsesTo (ParamId::hpFreq, "1.5 kHz", 1500.0f));
    CHECK (parsesTo (ParamId::hpFreq, "200 Hz", 200.0f));
    CHECK (parsesTo (ParamId::hpFreq, "200", 200.0f));
    CHECK (parsesTo (ParamId::hpFreq, "5", 20.0f)); // clamped to the range

    CHECK (parsesTo (ParamId::spacePreDelay, "0.1 s", 100.0f));
    CHECK (parsesTo (ParamId::spacePreDelay, "80 ms", 80.0f));

    CHECK (parsesTo (ParamId::inputGain, "-6 dB", -6.0f));
    CHECK (parsesTo (ParamId::inputGain,
                     juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92"
                                                           "6 dB")),
                     -6.0f));
    CHECK (parsesTo (ParamId::inputGain, "+3.5", 3.5f));

    CHECK (parsesTo (ParamId::keyScale, "dorian", 3.0f));
    CHECK (parsesTo (ParamId::keyScale, "2", 2.0f));
    CHECK (parsesTo (ParamId::pitchOctave, "-1", 1.0f));
    CHECK (parsesTo (ParamId::pitchOn, "off", 0.0f));
    CHECK (parsesTo (ParamId::pitchSemitones, "+7 st", 7.0f));

    CHECK_FALSE (parse (ParamId::mix, "loud").has_value());
    CHECK_FALSE (parse (ParamId::mix, "").has_value());
}

TEST_CASE ("Every displayed value reads back as the same value", "[unit][params]")
{
    for (const auto& def : kParamDefs)
    {
        const auto range = makeRange (def);

        for (int step = 0; step <= 20; ++step)
        {
            const auto value = range.convertFrom0to1 (static_cast<float> (step) / 20.0f);
            const auto shown = toText (def, value);
            const auto readBack = fromText (def, shown);

            INFO (std::string (def.id) << " = " << value << " shown as \"" << shown << "\"");
            REQUIRE (readBack.has_value());
            CHECK (toText (def, *readBack) == shown);
        }
    }
}

TEST_CASE ("Log parameters spread equal ratios evenly", "[unit][params]")
{
    // 20 Hz–2 kHz: the middle of the control is the geometric mean, 200 Hz.
    const auto hp = makeRange (paramDef (ParamId::hpFreq));
    CHECK_THAT (hp.convertFrom0to1 (0.0f), WithinRel (20.0f, 1e-5f));
    CHECK_THAT (hp.convertFrom0to1 (0.5f), WithinRel (200.0f, 1e-4f));
    CHECK_THAT (hp.convertFrom0to1 (1.0f), WithinRel (2000.0f, 1e-5f));
    CHECK_THAT (hp.convertTo0to1 (200.0f), WithinAbs (0.5, 1e-5));

    const auto mix = makeRange (paramDef (ParamId::mix));
    CHECK_THAT (mix.convertFrom0to1 (0.5f), WithinAbs (50.0, 1e-4));

    const auto semitones = makeRange (paramDef (ParamId::pitchSemitones));
    CHECK_THAT (semitones.snapToLegalValue (3.4f), WithinAbs (3.0, 1e-6));
}
