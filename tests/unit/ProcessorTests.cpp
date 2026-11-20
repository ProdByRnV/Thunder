// The real plugin processor, driven the way a host drives it: parameters,
// buses, saved state and processing.

#include "core/params/ParamDefs.h"
#include "core/state/StateIds.h"
#include "core/state/StateManager.h"
#include "plugin/ThunderProcessor.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <limits>
#include <string>

using namespace thunder;
using Catch::Matchers::WithinAbs;
using params::ParamId;

namespace
{
juce::String idOf (ParamId id)
{
    const auto text = params::paramDef (id).id;
    return juce::String::fromUTF8 (text.data(), static_cast<int> (text.size()));
}

juce::RangedAudioParameter& parameter (ThunderProcessor& p, ParamId id)
{
    auto* param = p.getValueTreeState().getParameter (idOf (id));
    REQUIRE (param != nullptr);
    return *param;
}

// Sets a parameter in real units, as a host or the editor would.
void setValue (ThunderProcessor& p, ParamId id, float value)
{
    auto& param = parameter (p, id);
    param.setValueNotifyingHost (param.convertTo0to1 (value));
}

float valueOf (ThunderProcessor& p, ParamId id)
{
    return p.getValueTreeState().getRawParameterValue (idOf (id))->load();
}
} // namespace

TEST_CASE ("The processor exposes every parameter from the table", "[unit][plugin]")
{
    ThunderProcessor processor;

    CHECK (static_cast<std::size_t> (processor.getParameters().size()) == params::kNumParams);
    CHECK (processor.getParameterTree().getSubgroups (false).size() == static_cast<int> (params::kGroupDefs.size()));

    for (const auto& def : params::kParamDefs)
    {
        INFO (std::string (def.id));
        auto& param = parameter (processor, def.param);

        CHECK (param.getVersionHint() == def.versionHint);
        CHECK (param.isAutomatable() == def.automatable);
        CHECK_THAT (param.convertFrom0to1 (param.getDefaultValue()), WithinAbs (def.def, 1e-3 * (def.max - def.min)));
        CHECK_THAT (valueOf (processor, def.param), WithinAbs (def.def, 1e-3 * (def.max - def.min)));
    }

    // Hosts show values in Thunder's formats.
    auto& hp = parameter (processor, ParamId::hpFreq);
    CHECK (hp.getText (hp.convertTo0to1 (200.0f), 32) == "200 Hz");
    CHECK_THAT (hp.convertFrom0to1 (hp.getValueForText ("1.5 kHz")), WithinAbs (1500.0, 0.01));
}

TEST_CASE ("Saved state restores every parameter and session setting", "[unit][plugin][state]")
{
    juce::MemoryBlock saved;
    state::SessionState session;
    session.presetName = "Test";
    session.captureBars = 2;

    {
        ThunderProcessor source;
        setValue (source, ParamId::mix, 80.0f);
        setValue (source, ParamId::hpFreq, 350.0f);
        setValue (source, ParamId::keyScale, 3.0f); // Dorian
        setValue (source, ParamId::pitchSemitones, -7.0f);
        setValue (source, ParamId::grainOn, 0.0f);
        source.setSessionState (session);
        source.getStateInformation (saved);
    }

    ThunderProcessor restored;
    restored.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));

    CHECK_THAT (valueOf (restored, ParamId::mix), WithinAbs (80.0, 1e-3));
    CHECK_THAT (valueOf (restored, ParamId::hpFreq), WithinAbs (350.0, 0.01));
    CHECK_THAT (valueOf (restored, ParamId::keyScale), WithinAbs (3.0, 1e-6));
    CHECK_THAT (valueOf (restored, ParamId::pitchSemitones), WithinAbs (-7.0, 1e-6));
    CHECK_THAT (valueOf (restored, ParamId::grainOn), WithinAbs (0.0, 1e-6));
    CHECK_THAT (valueOf (restored, ParamId::spaceMix), WithinAbs (40.0, 1e-3)); // untouched → default
    CHECK (restored.getSessionState() == session);
    CHECK_FALSE (restored.lastLoadWasFromNewerVersion());

    // The saved data is versioned.
    const auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), static_cast<int> (saved.getSize()));
    REQUIRE (xml != nullptr);
    CHECK (xml->getIntAttribute ("stateVersion") == state::kCurrentStateVersion);
}

TEST_CASE ("Unreadable state leaves the current settings alone", "[unit][plugin][state]")
{
    ThunderProcessor processor;
    setValue (processor, ParamId::mix, 65.0f);

    const char junk[] = "definitely not a Thunder state";
    processor.setStateInformation (junk, static_cast<int> (sizeof (junk)));
    processor.setStateInformation (nullptr, 0);

    CHECK_THAT (valueOf (processor, ParamId::mix), WithinAbs (65.0, 1e-3));
}

TEST_CASE ("State from a newer Thunder is loaded and flagged", "[unit][plugin][state]")
{
    juce::MemoryBlock saved;
    {
        ThunderProcessor source;
        setValue (source, ParamId::mix, 30.0f);
        source.getStateInformation (saved);
    }

    auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), static_cast<int> (saved.getSize()));
    REQUIRE (xml != nullptr);
    xml->setAttribute ("stateVersion", state::kCurrentStateVersion + 1);

    juce::MemoryBlock newer;
    juce::AudioProcessor::copyXmlToBinary (*xml, newer);

    ThunderProcessor processor;
    processor.setStateInformation (newer.getData(), static_cast<int> (newer.getSize()));

    CHECK (processor.lastLoadWasFromNewerVersion());
    CHECK_THAT (valueOf (processor, ParamId::mix), WithinAbs (30.0, 1e-3));
}

TEST_CASE ("Stereo and mono inputs are accepted; output is always stereo", "[unit][plugin]")
{
    ThunderProcessor processor;

    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (juce::AudioChannelSet::mono());
    layout.outputBuses.add (juce::AudioChannelSet::stereo());
    CHECK (processor.checkBusesLayoutSupported (layout));

    layout.inputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    CHECK (processor.checkBusesLayoutSupported (layout));

    layout.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
    CHECK_FALSE (processor.checkBusesLayoutSupported (layout));
}

TEST_CASE ("Mono input plays on both sides", "[unit][plugin]")
{
    ThunderProcessor processor;

    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (juce::AudioChannelSet::mono());
    layout.outputBuses.add (juce::AudioChannelSet::stereo());
    REQUIRE (processor.setBusesLayout (layout));

    // The reverb and the grains' pan spread are deliberately different left and right.
    setValue (processor, ParamId::spaceMix, 0.0f);
    setValue (processor, ParamId::grainPanSpread, 0.0f);
    processor.prepareToPlay (48000.0, 256);

    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buffer (2, 256);
    int position = 0;

    // Past the latency, so the shifted layer and the dry are both audible.
    for (int block = 0; block < 40; ++block)
    {
        for (int i = 0; i < 256; ++i, ++position)
        {
            buffer.setSample (0, i, std::sin (static_cast<float> (position) * 0.05f) * 0.5f);
            buffer.setSample (1, i, 0.9f); // whatever the host left there
        }
        processor.processBlock (buffer, midi);
    }

    // The same on both sides. Not bit for bit: the pitch shifter's floating-point
    // rounding differs slightly between its channels (measured up to ~1.5e-4,
    // about −75 dB below this signal), and the grains replay those samples.
    CHECK (buffer.getMagnitude (0, 0, 256) > 0.1f);
    for (int i = 0; i < 256; ++i)
        REQUIRE_THAT (buffer.getSample (1, i), WithinAbs (buffer.getSample (0, i), 5e-4));
}

TEST_CASE ("The processor survives what hosts throw at it", "[unit][plugin]")
{
    ThunderProcessor processor;
    processor.prepareToPlay (44100.0, 128);

    juce::MidiBuffer midi;

    // Empty block, then a block far larger than announced, full of bad samples.
    juce::AudioBuffer<float> empty (2, 0);
    processor.processBlock (empty, midi);

    juce::AudioBuffer<float> big (2, 8192);
    big.clear();
    big.setSample (0, 100, std::numeric_limits<float>::quiet_NaN());
    big.setSample (1, 5000, std::numeric_limits<float>::infinity());
    processor.processBlock (big, midi);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < big.getNumSamples(); ++i)
            REQUIRE (std::isfinite (big.getSample (ch, i)));

    // Sample-rate change and reset.
    processor.prepareToPlay (96000.0, 1024);
    processor.reset();
    processor.processBlock (big, midi);
    processor.releaseResources();

    CHECK (processor.hasEditor());
    CHECK (processor.getLatencySamples() == processor.getEngine().getLatencySamples());
}

TEST_CASE ("An offline bounce sounds exactly like real-time playback", "[unit][plugin][bounce]")
{
    // PRD NFR-REL-005, ROADMAP T-0.4-08. The host renders offline in big blocks
    // and plays back in small, sometimes irregular ones; Thunder must produce
    // the same samples either way. Every module is on at its defaults.
    constexpr double rate = 48000.0;
    juce::AudioBuffer<float> input (2, static_cast<int> (3.0 * rate));
    for (int i = 0; i < input.getNumSamples(); ++i)
    {
        const auto t = static_cast<double> (i) / rate;
        const auto note = 57.0 + 2.0 * std::floor (std::fmod (t * 4.0, 6.0));
        const auto hz = 440.0 * std::pow (2.0, (note - 69.0) / 12.0);
        const auto s = static_cast<float> (0.3 * std::sin (2.0 * juce::MathConstants<double>::pi * hz * t));
        input.setSample (0, i, s);
        input.setSample (1, i, 0.8f * s);
    }

    const auto render = [&] (bool offline, int hostBlock, bool irregular)
    {
        ThunderProcessor processor;
        processor.setNonRealtime (offline);
        processor.prepareToPlay (rate, hostBlock);
        processor.reset(); // hosts reset before a render

        juce::AudioBuffer<float> out (input);
        juce::MidiBuffer midi;
        const int pattern[] = {1, 37, 512, 1000, 64, 3};
        int start = 0;
        for (int k = 0; start < out.getNumSamples(); ++k)
        {
            const auto wanted = irregular ? pattern[k % 6] : hostBlock;
            const auto n = std::min (wanted, out.getNumSamples() - start);
            juce::AudioBuffer<float> block (out.getArrayOfWritePointers(), 2, start, n);
            processor.processBlock (block, midi);
            start += n;
        }
        return out;
    };

    const auto realtime = render (false, 128, false);
    const auto offline = render (true, 4096, false);
    const auto irregular = render (false, 1024, true);

    for (int ch = 0; ch < 2; ++ch)
    {
        INFO ("channel " << ch);
        float worstOffline = 0.0f, worstIrregular = 0.0f;
        for (int i = 0; i < realtime.getNumSamples(); ++i)
        {
            worstOffline = std::max (worstOffline, std::abs (offline.getSample (ch, i) - realtime.getSample (ch, i)));
            worstIrregular =
                std::max (worstIrregular, std::abs (irregular.getSample (ch, i) - realtime.getSample (ch, i)));
        }
        CHECK (juce::exactlyEqual (worstOffline, 0.0f));
        CHECK (juce::exactlyEqual (worstIrregular, 0.0f));
    }
    CHECK (realtime.getMagnitude (0, 0, realtime.getNumSamples()) > 0.1f);
}
