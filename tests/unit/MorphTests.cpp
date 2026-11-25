// The effective-parameter stage and Morph (PRD FR-PLAY-002, DEC-041;
// ARCHITECTURE §5.10; ROADMAP T-0.9-13): pass-through, the interpolation in
// each control's taper, exact endpoints, no clicks while sweeping, a bounce
// sounds like playback, and sound B saved with the project.

#include "support/TestSignals.h"

#include "core/engine/ThunderEngine.h"
#include "core/perform/PerformanceStage.h"
#include "core/perform/SoundB.h"
#include "core/state/StateIds.h"
#include "core/state/StateManager.h"
#include "core/util/TripleBuffer.h"
#include "plugin/ThunderEditor.h"
#include "plugin/ThunderProcessor.h"
#include "support/TestSettings.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <atomic>
#include <cmath>
#include <thread>

using namespace thunder;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using params::ParamId;
using params::ParamSnapshot;

namespace
{
constexpr double kSampleRate = 48000.0;

struct Stereo
{
    std::vector<float> left, right;
};

Stereo sineInput (double seconds)
{
    Stereo s;
    s.left = test::sine (220.0, kSampleRate, seconds, 0.25f);
    s.right = s.left;
    return s;
}

// A sound B well away from the defaults in every kind of control.
perform::MorphSound soundB()
{
    perform::MorphSound b;
    b.present = true;
    auto s = ParamSnapshot::defaults();
    s.set (ParamId::grainAmount, 90.0f);
    s.set (ParamId::spaceMix, 60.0f);
    s.set (ParamId::spaceDecay, 6.0f);
    s.set (ParamId::lpFreq, 2000.0f);
    s.set (ParamId::widthAmount, 160.0f);
    s.set (ParamId::layerLevel, -6.0f);
    s.set (ParamId::pitchOctave, 0.0f); // a choice: -1 octave
    s.set (ParamId::toneOn, 1.0f);      // a switch
    s.set (ParamId::keyRoot, 9.0f);     // never morphed
    b.values = s.values;
    return b;
}

// Renders `input` through an engine with sound B set, the snapshot chosen per block.
template<typename SnapshotFor>
Stereo render (const Stereo& input, int blockSize, const perform::MorphSound& b, SnapshotFor snapshotFor)
{
    ThunderEngine engine;
    engine.prepare (kSampleRate);
    engine.getPerformance().setMorphSound (b);

    auto output = input;
    for (std::size_t start = 0, block = 0; start < output.left.size();
         start += static_cast<std::size_t> (blockSize), ++block)
    {
        const auto n = std::min (static_cast<std::size_t> (blockSize), output.left.size() - start);
        float* channels[] = {output.left.data() + start, output.right.data() + start};
        engine.process (channels, static_cast<int> (n), {}, snapshotFor (block));
    }
    return output;
}

float largestStep (const std::vector<float>& s, std::size_t from)
{
    float largest = 0.0f;
    for (auto i = std::max<std::size_t> (from, 1); i < s.size(); ++i)
        largest = std::max (largest, std::abs (s[i] - s[i - 1]));
    return largest;
}

bool identical (const std::vector<float>& a, const std::vector<float>& b)
{
    return a.size() == b.size() &&
           std::equal (a.begin(), a.end(), b.begin(), [] (float x, float y) { return juce::exactlyEqual (x, y); });
}
} // namespace

// ---- The hand-over ------------------------------------------------------------------------------------------------

TEST_CASE ("The triple buffer hands over the newest complete value", "[unit][perform]")
{
    util::TripleBuffer<std::array<int, 64>> buffer;
    CHECK_FALSE (buffer.update());
    CHECK (buffer.get()[0] == 0);

    std::array<int, 64> value{};
    value.fill (1);
    buffer.write (value);
    value.fill (2);
    buffer.write (value); // replaces 1 before the reader looked

    CHECK (buffer.update());
    CHECK (buffer.get()[0] == 2);
    CHECK_FALSE (buffer.update()); // nothing newer
    CHECK (buffer.get()[63] == 2);

    // Under load: the reader never sees a half-written value, and it ends on the last.
    std::atomic<bool> done{false};
    std::atomic<int> torn{0};
    std::thread reader (
        [&]
        {
            while (!done.load())
                if (buffer.update())
                {
                    const auto& v = buffer.get();
                    for (const auto x : v)
                        if (x != v[0])
                            torn.fetch_add (1);
                }
        });

    for (int i = 3; i < 20000; ++i)
    {
        value.fill (i);
        buffer.write (value);
    }
    done.store (true);
    reader.join();

    buffer.update();
    CHECK (torn.load() == 0);
    CHECK (buffer.get()[0] == 19999);
}

// ---- Interpolation ------------------------------------------------------------------------------------------------

TEST_CASE ("Morph moves each control in its own taper", "[unit][perform]")
{
    using perform::morphValue;
    const auto& mix = params::paramDef (ParamId::spaceMix);         // linear %
    const auto& lp = params::paramDef (ParamId::lpFreq);            // logarithmic Hz
    const auto& octave = params::paramDef (ParamId::pitchOctave);   // choice
    const auto& on = params::paramDef (ParamId::toneOn);            // switch
    const auto& semis = params::paramDef (ParamId::pitchSemitones); // whole numbers

    // Endpoints are exact.
    for (const auto* def : {&mix, &lp, &octave, &on, &semis})
    {
        CHECK (juce::exactlyEqual (morphValue (*def, def->min, def->max, 0.0f), def->min));
        CHECK (juce::exactlyEqual (morphValue (*def, def->min, def->max, 1.0f), def->max));
    }

    CHECK_THAT (morphValue (mix, 20.0f, 60.0f, 0.5f), WithinAbs (40.0, 1e-5));
    CHECK_THAT (morphValue (mix, 20.0f, 60.0f, 0.25f), WithinAbs (30.0, 1e-5));

    // Frequencies by equal ratios: halfway from 1 kHz to 4 kHz is 2 kHz.
    CHECK_THAT (morphValue (lp, 1000.0f, 4000.0f, 0.5f), WithinRel (2000.0, 1e-4));

    // Whole numbers stay whole.
    CHECK (juce::exactlyEqual (morphValue (semis, 0.0f, 7.0f, 0.5f), std::round (3.5f)));

    // Switches and choices change at the halfway point.
    CHECK (juce::exactlyEqual (morphValue (octave, 1.0f, 0.0f, 0.49f), 1.0f));
    CHECK (juce::exactlyEqual (morphValue (octave, 1.0f, 0.0f, 0.5f), 0.0f));
    CHECK (juce::exactlyEqual (morphValue (on, 0.0f, 1.0f, 0.49f), 0.0f));
    CHECK (juce::exactlyEqual (morphValue (on, 0.0f, 1.0f, 0.51f), 1.0f));
}

TEST_CASE ("Key, Scale and Output Mode are never morphed", "[unit][perform]")
{
    const auto b = soundB();
    auto a = ParamSnapshot::defaults();
    a.set (ParamId::keyRoot, 2.0f);
    a.set (ParamId::outputMode, 1.0f);

    ParamSnapshot out;
    perform::applyMorph (a, b, 1.0f, out);
    CHECK (juce::exactlyEqual (out.get (ParamId::keyRoot), 2.0f));
    CHECK (juce::exactlyEqual (out.get (ParamId::keyScale), a.get (ParamId::keyScale)));
    CHECK (juce::exactlyEqual (out.get (ParamId::outputMode), 1.0f));
    CHECK (juce::exactlyEqual (out.get (ParamId::grainAmount), 90.0f));
    CHECK_FALSE (perform::isMorphed (ParamId::morphAmount));
}

TEST_CASE ("Morph never switches the safety limiter off", "[unit][perform]")
{
    auto b = soundB();
    b.values[params::toIndex (ParamId::safetyLimiter)] = 0.0f;
    const auto a = ParamSnapshot::defaults();
    REQUIRE (a.isOn (ParamId::safetyLimiter));

    ParamSnapshot out;
    perform::applyMorph (a, b, 1.0f, out);
    CHECK (out.isOn (ParamId::safetyLimiter));
    CHECK_FALSE (perform::isMorphed (ParamId::safetyLimiter));
}

TEST_CASE ("With no performance control active the host's values pass straight through", "[unit][perform]")
{
    perform::PerformanceStage stage;
    auto host = ParamSnapshot::defaults();

    // No B: the very same values, whatever Morph says.
    host.set (ParamId::morphAmount, 70.0f);
    CHECK (&stage.apply (host) == &host);

    // A B, but Morph at 0%.
    stage.setMorphSound (soundB());
    host.set (ParamId::morphAmount, 0.0f);
    CHECK (&stage.apply (host) == &host);

    // Morph above 0% moves them.
    host.set (ParamId::morphAmount, 50.0f);
    const auto& moved = stage.apply (host);
    CHECK (&moved != &host);
    CHECK_THAT (moved.get (ParamId::grainAmount), WithinAbs (70.0, 1e-4)); // 50 → 90

    // Cleared again.
    stage.setMorphSound ({});
    CHECK (&stage.apply (host) == &host);
}

// ---- In the engine ------------------------------------------------------------------------------------------------

TEST_CASE ("Morph's ends sound exactly like A and like B", "[unit][perform][engine]")
{
    const auto input = sineInput (1.0);
    const auto b = soundB();

    auto a = ParamSnapshot::defaults();
    auto bSnapshot = a;
    for (const auto& def : params::kParamDefs)
        if (perform::isMorphed (def.param))
            bSnapshot.values[params::toIndex (def.param)] = b.values[params::toIndex (def.param)];

    const auto none = perform::MorphSound{};

    // 0%: exactly A.
    const auto plainA = render (input, 512, none, [&] (std::size_t) { return a; });
    const auto morphedA = render (input, 512, b, [&] (std::size_t) { return a; });
    CHECK (identical (plainA.left, morphedA.left));
    CHECK (identical (plainA.right, morphedA.right));

    // 100%: exactly B.
    auto full = a;
    full.set (ParamId::morphAmount, 100.0f);
    const auto plainB = render (input, 512, none, [&] (std::size_t) { return bSnapshot; });
    const auto morphedB = render (input, 512, b, [&] (std::size_t) { return full; });
    CHECK (identical (plainB.left, morphedB.left));
    CHECK (identical (plainB.right, morphedB.right));
}

TEST_CASE ("Sweeping Morph never clicks", "[unit][perform][engine]")
{
    // A to B and back over two seconds, including the halfway switches (Tone
    // on, the octave choice): the largest step stays near the settled renders'.
    const auto input = sineInput (3.0);
    const auto b = soundB();
    auto a = ParamSnapshot::defaults();
    a.set (ParamId::spaceMix, 0.0f); // a dry reverb keeps steps easy to judge

    const auto blocks = static_cast<double> (input.left.size()) / 256.0;
    const auto swept =
        render (input, 256, b,
                [&] (std::size_t block)
                {
                    auto s = a;
                    const auto t = static_cast<double> (block) / blocks;
                    s.set (ParamId::morphAmount, static_cast<float> (100.0 * (t < 0.5 ? 2.0 * t : 2.0 - 2.0 * t)));
                    return s;
                });

    auto atB = a;
    atB.set (ParamId::morphAmount, 100.0f);
    const auto settledA = render (input, 256, b, [&] (std::size_t) { return a; });
    const auto settledB = render (input, 256, b, [&] (std::size_t) { return atB; });

    const auto from = static_cast<std::size_t> (0.5 * kSampleRate);
    const auto steady = std::max (largestStep (settledA.left, from), largestStep (settledB.left, from));
    CHECK (test::allFinite (swept.left));
    CHECK (largestStep (swept.left, from) < 1.25f * steady + 0.005f);
}

TEST_CASE ("A morphed bounce sounds exactly like playback", "[unit][perform][engine]")
{
    const auto input = sineInput (1.0);
    const auto b = soundB();
    auto s = ParamSnapshot::defaults();
    s.set (ParamId::morphAmount, 40.0f);

    const auto offline = render (input, 4096, b, [&] (std::size_t) { return s; });
    const auto playback = render (input, 256, b, [&] (std::size_t) { return s; });
    CHECK (identical (offline.left, playback.left));
    CHECK (identical (offline.right, playback.right));
}

// ---- Sound B in the project ---------------------------------------------------------------------------------------

TEST_CASE ("Sound B is saved with the project and read back through the preset reader", "[unit][perform][state]")
{
    auto b = perform::SoundB::defaults ("Night Static");
    b.values[params::toIndex (ParamId::grainAmount)] = 77.0f;
    b.values[params::toIndex (ParamId::spaceMode)] = 2.0f;

    const auto tree = b.toValueTree ("0.9.0");
    CHECK (tree.hasType (state::ids::morphSoundB));
    const auto back = perform::SoundB::fromValueTree (tree);
    REQUIRE (back.has_value());
    CHECK (*back == b);

    // A damaged one is no B, not a crash.
    juce::ValueTree damaged{state::ids::morphSoundB};
    damaged.appendChild (juce::ValueTree{"NotAPreset"}, nullptr);
    CHECK_FALSE (perform::SoundB::fromValueTree (damaged).has_value());
    CHECK_FALSE (perform::SoundB::fromValueTree ({}).has_value());

    // An older B goes through the same migrations as everything else: Plate in
    // a version-1 sound played the Cloud reverb (DEC-050).
    auto old = tree.createCopy();
    auto preset = old.getChild (0);
    preset.setProperty (state::ids::stateVersion, 1, nullptr);
    const auto migrated = perform::SoundB::fromValueTree (old);
    REQUIRE (migrated.has_value());
    CHECK (juce::exactlyEqual (migrated->values[params::toIndex (ParamId::spaceMode)], 0.0f));
}

TEST_CASE ("The processor keeps sound B with the project, never in presets", "[unit][perform][state]")
{
    const auto settings = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("ThunderMorphTests-" + juce::Uuid().toString());
    settings.createDirectory();

    ThunderProcessor saved;
    saved.setSettingsFile (settings.getChildFile ("settings.xml"));
    CHECK_FALSE (saved.getSoundB().has_value());
    CHECK_FALSE (saved.getMorphedValues().has_value());

    auto b = perform::SoundB::defaults ("Glass Choir");
    b.values[params::toIndex (ParamId::grainAmount)] = 10.0f;
    saved.setSoundB (b);

    auto* morph = saved.getValueTreeState().getParameter ("morphAmount");
    morph->setValueNotifyingHost (morph->convertTo0to1 (50.0f));
    const auto morphed = saved.getMorphedValues();
    REQUIRE (morphed.has_value());
    CHECK_THAT (morphed->get (ParamId::grainAmount), WithinAbs (30.0, 1e-4)); // 50 → 10

    juce::MemoryBlock data;
    saved.getStateInformation (data);

    ThunderProcessor restored;
    restored.setSettingsFile (settings.getChildFile ("settings.xml"));
    restored.setStateInformation (data.getData(), static_cast<int> (data.getSize()));
    REQUIRE (restored.getSoundB().has_value());
    CHECK (*restored.getSoundB() == b);

    // A project without B clears it.
    ThunderProcessor plain;
    juce::MemoryBlock plainData;
    plain.getStateInformation (plainData);
    restored.setStateInformation (plainData.getData(), static_cast<int> (plainData.getSize()));
    CHECK_FALSE (restored.getSoundB().has_value());

    // Morph's amount isn't part of a preset's sound: moving it doesn't modify one.
    CHECK_FALSE (saved.getPresetManager().getCurrent().modified);

    settings.deleteRecursively();
}

TEST_CASE ("Morph in the top bar sets, shows and clears sound B", "[unit][perform][ui]")
{
    const auto settings = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("ThunderMorphTests-" + juce::Uuid().toString());
    settings.createDirectory();
    test::chooseMotion (settings.getChildFile ("settings.xml"), false);

    ThunderProcessor processor;
    processor.setSettingsFile (settings.getChildFile ("settings.xml"));
    processor.getPresetManager().setUserFolder (settings.getChildFile ("Presets"));
    auto editor = std::make_unique<ThunderEditor> (processor);
    auto& morph = editor->getTopBar().getMorph();

    // No B: disabled, and it says so.
    CHECK_FALSE (morph.hasSoundB());
    CHECK_FALSE (morph.getSlider().isEnabled());
    CHECK (morph.getTooltipText().contains ("Set sound B first"));

    // Set B = current sound, then move away from it.
    auto* grain = processor.getValueTreeState().getParameter ("grainAmount");
    grain->setValueNotifyingHost (grain->convertTo0to1 (20.0f));
    morph.chooseMenuItem (ui::MorphControl::kMenuSetCurrent);
    REQUIRE (processor.getSoundB().has_value());
    CHECK (morph.hasSoundB());
    CHECK (morph.getSlider().isEnabled());
    CHECK (morph.getTooltipText().contains ("B: Init"));

    grain->setValueNotifyingHost (grain->convertTo0to1 (80.0f));
    morph.getSlider().setValue (50.0, juce::sendNotificationSync);
    editor->updateMorph();

    // Knobs show what Morph sends as a second ring.
    ui::ThunderKnob* amountKnob = nullptr;
    for (auto* knob : editor->getKnobs())
        if (knob->getParamId() == ParamId::grainAmount)
            amountKnob = knob;
    REQUIRE (amountKnob != nullptr);
    REQUIRE (amountKnob->getMorphedValue().has_value());
    CHECK_THAT (*amountKnob->getMorphedValue(), WithinAbs (50.0, 1e-3)); // 80 → 20

    // Load B from a preset.
    const auto menu = morph.buildMenu();
    CHECK (menu.getNumItems() >= 3);
    morph.chooseMenuItem (ui::MorphControl::kMenuFirstPreset);
    REQUIRE (processor.getSoundB().has_value());
    CHECK (processor.getSoundB()->name == presets::factoryPresets().front().preset.meta.name);

    // Clear B: the rings go.
    morph.chooseMenuItem (ui::MorphControl::kMenuClear);
    editor->updateMorph();
    CHECK_FALSE (processor.getSoundB().has_value());
    CHECK_FALSE (morph.getSlider().isEnabled());
    CHECK_FALSE (amountKnob->getMorphedValue().has_value());

    editor.reset();
    settings.deleteRecursively();
}
