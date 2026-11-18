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

    processor.prepareToPlay (48000.0, 256);

    juce::AudioBuffer<float> buffer (2, 256);
    for (int i = 0; i < 256; ++i)
    {
        buffer.setSample (0, i, std::sin (static_cast<float> (i) * 0.1f) * 0.5f);
        buffer.setSample (1, i, 0.9f); // whatever the host left there
    }

    juce::MidiBuffer midi;
    processor.processBlock (buffer, midi);

    for (int i = 0; i < 256; ++i)
        REQUIRE_THAT (buffer.getSample (1, i), WithinAbs (buffer.getSample (0, i), 1e-9));
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
    CHECK (processor.getLatencySamples() == 0);
}
