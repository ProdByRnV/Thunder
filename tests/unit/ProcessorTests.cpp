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
#include <vector>

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
    // The knobs moved away from the loaded sound (Init), so it is saved as modified.
    auto expected = session;
    expected.presetModified = true;
    CHECK (restored.getSessionState() == expected);
    CHECK (restored.getPresetManager().getCurrent().modified);
    CHECK_FALSE (restored.lastLoadWasFromNewerVersion());

    // The saved data is versioned.
    const auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), static_cast<int> (saved.getSize()));
    REQUIRE (xml != nullptr);
    CHECK (xml->getIntAttribute ("stateVersion") == state::kCurrentStateVersion);
}

TEST_CASE ("A project saved before v1.0 opens with its sound and settings", "[unit][plugin][state]")
{
    // tests/fixtures/state/state_v1.xml was saved by Thunder 0.6.0 (state
    // version 1) with these settings changed, and Space set to Room, which
    // played the Cloud reverb then (QA-STATE-001, PRD DEC-050).
    const auto file = juce::File (THUNDER_TEST_FIXTURES).getChildFile ("state/state_v1.xml");
    const auto xml = juce::parseXML (file);
    REQUIRE (xml != nullptr);
    REQUIRE (xml->getIntAttribute ("stateVersion") == 1);

    juce::MemoryBlock saved;
    juce::AudioProcessor::copyXmlToBinary (*xml, saved);

    ThunderProcessor processor;
    processor.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));

    CHECK_THAT (valueOf (processor, ParamId::mix), WithinAbs (70.0, 1e-3));
    CHECK_THAT (valueOf (processor, ParamId::keyRoot), WithinAbs (9.0, 1e-6));
    CHECK_THAT (valueOf (processor, ParamId::pitchMode), WithinAbs (2.0, 1e-6));
    CHECK_THAT (valueOf (processor, ParamId::pitchSemitones), WithinAbs (7.0, 1e-6));
    CHECK_THAT (valueOf (processor, ParamId::grainPitchSpread), WithinAbs (5.0, 1e-4));
    CHECK_THAT (valueOf (processor, ParamId::grainPosSpread), WithinAbs (60.0, 1e-3));
    CHECK_THAT (valueOf (processor, ParamId::spaceDecay), WithinAbs (70.0, 1e-3));
    CHECK_THAT (valueOf (processor, ParamId::hpFreq), WithinAbs (300.0, 0.01));
    CHECK_THAT (valueOf (processor, ParamId::widthAmount), WithinAbs (150.0, 1e-3));

    // Migrated: the reverb it played, and grain pitches as free as they were.
    CHECK_THAT (valueOf (processor, ParamId::spaceMode), WithinAbs (0.0, 1e-6));
    CHECK_THAT (valueOf (processor, ParamId::grainScaleLock), WithinAbs (0.0, 1e-6));

    // Everything new is at its default, with the new modules off.
    for (const auto& def : params::kParamDefs)
        if (def.versionHint == params::kReleaseV1_0 && def.param != ParamId::grainScaleLock)
        {
            INFO (std::string (def.id));
            CHECK_THAT (valueOf (processor, def.param), WithinAbs (def.def, 1e-3 * (def.max - def.min)));
        }

    const auto session = processor.getSessionState();
    CHECK (session.presetName == "Night Static");
    CHECK (session.presetKey == "factory:Night Static");
    CHECK (session.captureBars == 8);
    CHECK (juce::exactlyEqual (session.uiScale, 1.25f));
    CHECK (session.uiView == state::VisualiserView::spectrum);
    CHECK_FALSE (processor.lastLoadWasFromNewerVersion());

    // Saved again, it is a version 2 state that keeps all of that.
    juce::MemoryBlock resaved;
    processor.getStateInformation (resaved);
    const auto again = juce::AudioProcessor::getXmlFromBinary (resaved.getData(), static_cast<int> (resaved.getSize()));
    REQUIRE (again != nullptr);
    CHECK (again->getIntAttribute ("stateVersion") == 2);

    ThunderProcessor reopened;
    reopened.setStateInformation (resaved.getData(), static_cast<int> (resaved.getSize()));
    CHECK_THAT (valueOf (reopened, ParamId::grainScaleLock), WithinAbs (0.0, 1e-6));
    CHECK_THAT (valueOf (reopened, ParamId::mix), WithinAbs (70.0, 1e-3));
}

TEST_CASE ("A version 2 state keeps Plate and Scale Lock as saved", "[unit][plugin][state]")
{
    juce::MemoryBlock saved;
    {
        ThunderProcessor source;
        setValue (source, ParamId::spaceMode, 1.0f); // Plate
        source.getStateInformation (saved);
    }

    ThunderProcessor restored;
    restored.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    CHECK_THAT (valueOf (restored, ParamId::spaceMode), WithinAbs (1.0, 1e-6));
    CHECK_THAT (valueOf (restored, ParamId::grainScaleLock), WithinAbs (1.0, 1e-6));
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

TEST_CASE ("Bypassed, Thunder plays its input delayed by its latency, on both sides", "[unit][plugin]")
{
    for (const auto mono : {false, true})
    {
        ThunderProcessor processor;
        if (mono)
        {
            juce::AudioProcessor::BusesLayout layout;
            layout.inputBuses.add (juce::AudioChannelSet::mono());
            layout.outputBuses.add (juce::AudioChannelSet::stereo());
            REQUIRE (processor.setBusesLayout (layout));
        }
        processor.prepareToPlay (48000.0, 256);
        const auto latency = processor.getLatencySamples();
        REQUIRE (latency > 0);

        // The impulse comes after the 20 ms fade into bypass.
        constexpr int kBlock = 256;
        constexpr int impulseAt = kBlock * 8 + 17;
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> buffer (2, kBlock);
        std::vector<float> left, right;
        for (int block = 0; block < 12 + latency / kBlock; ++block)
        {
            buffer.clear();
            const auto start = block * kBlock;
            if (impulseAt >= start && impulseAt < start + kBlock)
                for (int ch = 0; ch < (mono ? 1 : 2); ++ch)
                    buffer.setSample (ch, impulseAt - start, 1.0f);

            processor.processBlockBypassed (buffer, midi);
            for (int i = 0; i < kBlock; ++i)
            {
                left.push_back (buffer.getSample (0, i));
                right.push_back (buffer.getSample (1, i));
            }
        }

        const auto at = static_cast<std::size_t> (impulseAt + latency);
        CHECK_THAT (left[at], WithinAbs (1.0, 1e-6));
        CHECK_THAT (right[at], WithinAbs (1.0, 1e-6));
        CHECK_THAT (left[at - 1], WithinAbs (0.0, 1e-6));
        CHECK_THAT (left[static_cast<std::size_t> (impulseAt)], WithinAbs (0.0, 1e-6));
    }
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

namespace
{
// A few seconds of a stepping melody, the same every time.
juce::AudioBuffer<float> melody (double rate, double seconds)
{
    juce::AudioBuffer<float> input (2, static_cast<int> (seconds * rate));
    for (int i = 0; i < input.getNumSamples(); ++i)
    {
        const auto t = static_cast<double> (i) / rate;
        const auto note = 60.0 + 2.0 * std::floor (std::fmod (t * 3.0, 5.0));
        const auto hz = 440.0 * std::pow (2.0, (note - 69.0) / 12.0);
        const auto s = static_cast<float> (0.3 * std::sin (2.0 * juce::MathConstants<double>::pi * hz * t));
        input.setSample (0, i, s);
        input.setSample (1, i, 0.7f * s);
    }
    return input;
}

// Plays `input` through the processor in 256-sample blocks.
juce::AudioBuffer<float> render (ThunderProcessor& processor, const juce::AudioBuffer<float>& input)
{
    juce::AudioBuffer<float> out (input);
    juce::MidiBuffer midi;
    for (int start = 0; start < out.getNumSamples(); start += 256)
    {
        const auto n = std::min (256, out.getNumSamples() - start);
        juce::AudioBuffer<float> block (out.getArrayOfWritePointers(), 2, start, n);
        processor.processBlock (block, midi);
    }
    return out;
}

bool identical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < a.getNumSamples(); ++i)
            if (!juce::exactlyEqual (a.getSample (ch, i), b.getSample (ch, i)))
                return false;
    return true;
}

// Settings away from the defaults in every module (grains included, so their
// seeded randomness is covered too).
void setNonDefaults (ThunderProcessor& p)
{
    setValue (p, ParamId::pitchSemitones, -5.0f);
    setValue (p, ParamId::grainPitchSpread, 7.0f);
    setValue (p, ParamId::grainPanSpread, 80.0f);
    setValue (p, ParamId::spaceDecay, 70.0f);
    setValue (p, ParamId::hpFreq, 320.0f);
    setValue (p, ParamId::mix, 70.0f);
}
} // namespace

TEST_CASE ("Reopening a saved session sounds identical", "[unit][plugin][state]")
{
    // PRD NFR-REL-001. The host restores the state, then gets the plugin ready
    // to play (or, on a reload during a session, restores it and resets).
    constexpr double rate = 48000.0;
    const auto input = melody (rate, 3.0);

    juce::MemoryBlock saved;
    juce::AudioBuffer<float> original;
    {
        ThunderProcessor source;
        setNonDefaults (source);
        source.getStateInformation (saved);
        source.prepareToPlay (rate, 256);
        original = render (source, input);
    }
    CHECK (original.getMagnitude (0, 0, original.getNumSamples()) > 0.1f);

    SECTION ("State restored before playback starts")
    {
        ThunderProcessor reopened;
        reopened.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        reopened.prepareToPlay (rate, 256);
        CHECK (identical (render (reopened, input), original));
    }

    SECTION ("State restored into a running instance, then a reset")
    {
        ThunderProcessor reopened;
        reopened.prepareToPlay (rate, 256);
        render (reopened, melody (rate, 0.5)); // something else played first
        reopened.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        reopened.reset();
        CHECK (identical (render (reopened, input), original));
    }
}

TEST_CASE ("Instances don't affect each other", "[unit][plugin]")
{
    // PRD NFR-REL-004: two instances with different settings, playing block by
    // block side by side, sound exactly as they do alone.
    constexpr double rate = 48000.0;
    const auto input = melody (rate, 2.0);

    const auto alone = [&] (bool changed)
    {
        ThunderProcessor p;
        if (changed)
            setNonDefaults (p);
        p.prepareToPlay (rate, 256);
        return render (p, input);
    };
    const auto aloneA = alone (false);
    const auto aloneB = alone (true);

    ThunderProcessor a, b;
    setNonDefaults (b);
    a.prepareToPlay (rate, 256);
    b.prepareToPlay (rate, 256);

    juce::AudioBuffer<float> outA (input), outB (input);
    juce::MidiBuffer midi;
    for (int start = 0; start < input.getNumSamples(); start += 256)
    {
        const auto n = std::min (256, input.getNumSamples() - start);
        juce::AudioBuffer<float> blockA (outA.getArrayOfWritePointers(), 2, start, n);
        juce::AudioBuffer<float> blockB (outB.getArrayOfWritePointers(), 2, start, n);
        a.processBlock (blockA, midi);
        b.processBlock (blockB, midi);
    }

    CHECK (identical (outA, aloneA));
    CHECK (identical (outB, aloneB));
    CHECK_FALSE (identical (outA, outB));
}

TEST_CASE ("Opening and closing the editor over and over while playing is safe", "[unit][plugin][ui]")
{
    // PRD NFR-REL-003 ("editor open/close spam").
    ThunderProcessor processor;
    processor.prepareToPlay (48000.0, 256);
    const auto input = melody (48000.0, 0.1);

    for (int round = 0; round < 25; ++round)
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
        REQUIRE (editor != nullptr);
        setValue (processor, ParamId::mix, static_cast<float> ((round * 37) % 100));
        const auto out = render (processor, input);
        for (int ch = 0; ch < 2; ++ch)
            REQUIRE (std::isfinite (out.getMagnitude (ch, 0, out.getNumSamples())));
        processor.editorBeingDeleted (editor.get()); // as the host wrapper does
        editor.reset();
    }
    CHECK (processor.getActiveEditor() == nullptr);
}

TEST_CASE ("A reverb tail fades to silence without denormals", "[unit][plugin][space]")
{
    // PRD FR-SPACE-005: as a tail decays, its numbers would become denormal
    // (very slow on most CPUs). The audio path flushes them to zero, on Intel and
    // ARM alike, so the tail ends in true silence instead.
    constexpr double rate = 48000.0;
    ThunderProcessor processor;
    for (const auto id : {ParamId::pitchOn, ParamId::grainOn, ParamId::autoGain})
        setValue (processor, id, 0.0f);
    setValue (processor, ParamId::outputMode, static_cast<float> (static_cast<int> (params::OutputMode::LayerOnly)));
    setValue (processor, ParamId::spaceMix, 100.0f);
    setValue (processor, ParamId::spaceDecay, 0.0f);
    processor.prepareToPlay (rate, 256);

    juce::AudioBuffer<float> input (2, static_cast<int> (20.0 * rate));
    input.clear();
    for (int i = 0; i < 4800; ++i) // a 100 ms burst, then silence
        for (int ch = 0; ch < 2; ++ch)
            input.setSample (ch, i, 0.5f * std::sin (static_cast<float> (i) * 0.07f));

    const auto out = render (processor, input);
    CHECK (out.getMagnitude (0, 0, static_cast<int> (rate)) > 0.01f); // the reverb is audible

    int subnormals = 0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < out.getNumSamples(); ++i)
            if (std::fpclassify (out.getSample (ch, i)) == FP_SUBNORMAL)
                ++subnormals;
    CHECK (subnormals == 0);

    const auto lastSecond = static_cast<int> (19.0 * rate);
    CHECK (out.getMagnitude (lastSecond, out.getNumSamples() - lastSecond) < 1.0e-10f); // below −200 dB
}

TEST_CASE ("Restoring state puts back switches a host left between on and off", "[unit][plugin][state]")
{
    // What pluginval's "Plugin state restoration" does through a VST3 host:
    // one switch gets a raw value between 0 and 1 (0.67 reads as on), then the
    // saved state (on) is restored and the raw value read back.
    ThunderProcessor processor;
    auto& apvts = processor.getValueTreeState();
    juce::MemoryBlock saved;
    processor.getStateInformation (saved);

    for (const auto* id : {"pitchOn", "toneOn", "timeOn", "autoGain"})
    {
        INFO (id);
        auto* p = apvts.getParameter (id);
        const auto original = p->getValue();
        const auto between = original > 0.5f ? 0.67f : 0.27f; // the same side as the saved value

        p->setValue (between); // as the VST3 wrapper passes a host's value on
        p->sendValueChangedMessageToListeners (between);
        processor.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        CHECK_THAT (p->getValue(), WithinAbs (original, 1e-6));
    }

    // The other side is restored too.
    auto* p = apvts.getParameter ("pitchOn");
    p->setValue (0.2f);
    p->sendValueChangedMessageToListeners (0.2f);
    processor.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    CHECK_THAT (p->getValue(), WithinAbs (1.0, 1e-6));
}
