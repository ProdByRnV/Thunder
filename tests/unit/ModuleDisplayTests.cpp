// The module panels' live displays (ROADMAP T-0.8-10/11; PRD UI-GEN-001;
// UI.md §11.3, §11.4, §11.5, §11.7, §11.8, §11.9): the frames carry what the
// modules did, the editor's model keeps Static's trace and Duck's reduction,
// each display maps its data as described, the panels ask for the data they
// show, and Space's Decay reads in seconds for its mode. Headless: no window
// is shown.

#include "support/TestHost.h"
#include "support/TestSignals.h"

#include "core/dsp/shape/ShapeModule.h"
#include "core/dsp/space/SpaceModule.h"
#include "core/dsp/static/StaticModulator.h"
#include "core/visual/VisualTap.h"
#include "plugin/ThunderEditor.h"
#include "plugin/ThunderProcessor.h"
#include "plugin/ui/components/ModuleDisplays.h"
#include "plugin/ui/regions/ModulePanels.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace thunder;
using Catch::Matchers::WithinAbs;
using params::ParamId;
using visual::VisualTap;

namespace
{
constexpr double kRate = 48000.0;

// Module panel indices, in the chain's order.
constexpr int kGrainsPanel = 2;
constexpr int kStaticPanel = 3;
constexpr int kSpacePanel = 4;
constexpr int kShapePanel = 7;
constexpr int kDuckPanel = 8;

struct TempFolder
{
    juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("ThunderModuleDisplayTests-" + juce::Uuid().toString());
    TempFolder() { folder.createDirectory(); }
    ~TempFolder() { folder.deleteRecursively(); }
};

struct Fixture
{
    TempFolder temp;
    ThunderProcessor processor;
    test::TestPlayHead head;
    std::unique_ptr<juce::AudioProcessorEditor> editorOwner;
    ThunderEditor* editor = nullptr;

    Fixture()
    {
        processor.setSettingsFile (temp.folder.getChildFile ("settings.xml"));
        processor.getPresetManager().setUserFolder (temp.folder.getChildFile ("Presets"));
        processor.prepareToPlay (kRate, 512);
        processor.setPlayHead (&head);
        editorOwner.reset (processor.createEditorAndMakeActive());
        editor = dynamic_cast<ThunderEditor*> (editorOwner.get());
        REQUIRE (editor != nullptr);
    }

    ~Fixture()
    {
        processor.editorBeingDeleted (editorOwner.get());
        editorOwner.reset();
        processor.setPlayHead (nullptr);
    }

    // Plays the melody with a visualiser frame after every block; `everyBlock`
    // runs after each frame.
    void play (double seconds, const std::function<void()>& everyBlock = {})
    {
        editor->updateVisuals (true, 0.0);
        test::playMelody (processor, head, seconds, kRate, 512,
                          [this, &everyBlock]
                          {
                              editor->updateVisuals (true, 512.0 / kRate);
                              if (everyBlock)
                                  everyBlock();
                          });
    }

    juce::RangedAudioParameter& param (ParamId id) { return ui::parameter (processor.getValueTreeState(), id); }
    void set (ParamId id, float value) { param (id).setValueNotifyingHost (param (id).convertTo0to1 (value)); }
    void setChoice (ParamId id, int index) { set (id, static_cast<float> (index)); }

    template<typename Panel> Panel& panel (int index)
    {
        auto* p = dynamic_cast<Panel*> (&editor->getModulePanel (index));
        REQUIRE (p != nullptr);
        return *p;
    }
};

juce::Component* findById (juce::Component& root, const juce::String& id)
{
    if (root.getComponentID() == id)
        return &root;
    for (auto* child : root.getChildren())
        if (auto* found = findById (*child, id))
            return found;
    return nullptr;
}
} // namespace

// ---- The data path
// ---------------------------------------------------------------------------------------------------

TEST_CASE ("Frames carry Duck's largest reduction and the modules' latest state", "[unit][visual][displays]")
{
    VisualTap tap;
    tap.prepare (kRate, 256, 0);
    tap.setWanted (VisualTap::kFrames);
    tap.beginBlock();

    // Two chunks of 32 samples make one 64-sample frame.
    std::vector<float> silence (32, 0.0f);
    const float* channels[] = {silence.data(), silence.data()};
    TransportState t;
    t.hasHostInfo = true;
    t.isPlaying = true;

    visual::ModuleActivity first;
    first.duckReductionDb = 6.0f;
    first.staticValues = {0.1f, 0.0f, -0.5f, 0.2f, 0.0f};
    first.shapePhase = 0.25f;
    first.shapeGain = 0.5f;
    visual::ModuleActivity second = first;
    second.duckReductionDb = 2.0f;
    second.staticValues[0] = 0.3f;
    second.shapePhase = 0.26f;

    tap.tapLayer (channels, channels, 32);
    tap.tapOutput (channels, 32, t, 0.0f, first);
    tap.tapLayer (channels, channels, 32);
    tap.tapOutput (channels, 32, t.advancedBy (32, kRate), 0.0f, second);

    std::vector<visual::WaveFrame> frames (4);
    REQUIRE (tap.readFrames (frames.data(), 4) == 1);
    const auto& a = frames[0].activity;
    CHECK (juce::exactlyEqual (a.duckReductionDb, 6.0f));
    CHECK (juce::exactlyEqual (a.staticValues[0], 0.3f));
    CHECK (juce::exactlyEqual (a.staticValues[2], -0.5f));
    CHECK (juce::exactlyEqual (a.shapePhase, 0.26f));
    CHECK (juce::exactlyEqual (a.shapeGain, 0.5f));
}

TEST_CASE ("While Duck works the editor sees its reduction; off, none", "[unit][visual][displays][duck]")
{
    Fixture f;
    f.editor->selectModule (kDuckPanel);
    auto& meter = f.panel<ui::DuckPanel> (kDuckPanel).getMeter();

    f.set (ParamId::duckOn, 1.0f);
    f.set (ParamId::duckAmount, 100.0f);
    auto largest = 0.0f;
    f.play (2.0, [&] { largest = std::max (largest, f.editor->getVisualModel().getDuckReductionDb()); });
    CHECK (largest > 3.0f);
    CHECK (largest <= dsp::DuckModule::kMaxReductionDb + 0.01f);
    CHECK (meter.getValueText().startsWith (juce::String::fromUTF8 ("\xe2\x88\x92")));

    f.set (ParamId::duckOn, 0.0f);
    f.play (1.0);
    CHECK (f.editor->getVisualModel().getDuckReductionDb() < 0.01f);
    CHECK (meter.getValueText() == "0.0 dB");
}

TEST_CASE ("Shape's playhead follows the song through each cycle", "[unit][visual][displays][shape]")
{
    Fixture f;
    f.editor->selectModule (kShapePanel);
    f.set (ParamId::shapeOn, 1.0f);
    f.setChoice (ParamId::shapeRate, 1); // 1 bar: 2 s at 120 BPM

    std::vector<float> phases;
    f.play (4.5, [&] { phases.push_back (f.editor->getVisualModel().getActivity().shapePhase); });
    REQUIRE (phases.size() > 100);

    // Within a cycle the phase only moves forward; it wraps once a cycle (2 s,
    // 187.5 blocks of 512). The first wrap can come early: the song position
    // Shape sees starts just before 0 (the chain latency before it).
    std::vector<std::size_t> wraps;
    for (std::size_t i = 1; i < phases.size(); ++i)
    {
        const auto step = phases[i] - phases[i - 1];
        if (step < -0.5f)
            wraps.push_back (i);
        else
            CHECK (step >= 0.0f);
        CHECK (phases[i] >= 0.0f);
        CHECK (phases[i] < 1.0f);
    }
    REQUIRE (wraps.size() >= 2);
    for (std::size_t w = 1; w < wraps.size(); ++w)
        CHECK (std::abs (static_cast<double> (wraps[w] - wraps[w - 1]) - 187.5) <= 1.0);
}

TEST_CASE ("Static's trace holds two seconds of each target's movement", "[unit][visual][displays][static]")
{
    Fixture f;
    f.editor->selectModule (kStaticPanel);
    auto& model = f.editor->getVisualModel();
    using Target = dsp::StaticModulator::Target;

    // Off: the trace stays flat.
    f.play (0.5);
    for (int i = 0; i < ui::VisualModel::kTracePoints; ++i)
        CHECK (juce::exactlyEqual (model.getStaticTrace (Target::pan, i), 0.0f));

    f.set (ParamId::staticOn, 1.0f);
    f.set (ParamId::staticAmount, 100.0f);
    f.set (ParamId::staticTgtPan, 1.0f);
    f.set (ParamId::staticRate, 4.0f);
    f.play (2.5);

    // The newest points move; Pan stays within −1 … +1 and Position within 0 … 1.
    auto lowest = 1.0f, highest = -1.0f;
    for (int i = 0; i < ui::VisualModel::kTracePoints; ++i)
    {
        const auto pan = model.getStaticTrace (Target::pan, i);
        lowest = std::min (lowest, pan);
        highest = std::max (highest, pan);
        CHECK (pan >= -1.0f);
        CHECK (pan <= 1.0f);
        const auto position = model.getStaticTrace (Target::position, i);
        CHECK (position >= 0.0f);
        CHECK (position <= 1.0f);
    }
    CHECK (highest - lowest > 0.2f);

    // The newest point is within one point (10 ms) of the newest frame.
    CHECK_THAT (model.getStaticTrace (Target::pan, ui::VisualModel::kTracePoints - 1),
                WithinAbs (model.getActivity().staticValues[static_cast<std::size_t> (Target::pan)], 0.1));
    // Out of range reads are zero, never past the buffer.
    CHECK (juce::exactlyEqual (model.getStaticTrace (Target::pan, -1), 0.0f));
    CHECK (juce::exactlyEqual (model.getStaticTrace (7, 0), 0.0f));
}

// ---- What the displays draw
// ------------------------------------------------------------------------------------------

TEST_CASE ("The grain cloud places grains by age and pitch", "[unit][ui][displays][grains]")
{
    const juce::Rectangle<float> area (0.0f, 0.0f, 120.0f, 60.0f);
    visual::GrainSpark spark;

    spark.ageSeconds = 0.0f;
    CHECK_THAT (ui::GrainCloudView::dotPosition (spark, area).x, WithinAbs (0.0, 1.0e-4));
    CHECK_THAT (ui::GrainCloudView::dotPosition (spark, area).y, WithinAbs (30.0, 1.0e-4));

    // Square-root scale: a quarter of the way back reads half way across.
    spark.ageSeconds = ui::GrainCloudView::kMaxAgeSeconds / 4.0f;
    CHECK_THAT (ui::GrainCloudView::dotPosition (spark, area).x, WithinAbs (60.0, 1.0e-3));

    spark.ageSeconds = 100.0f; // clamped to the right edge
    spark.semitones = 12.0f;
    CHECK_THAT (ui::GrainCloudView::dotPosition (spark, area).x, WithinAbs (120.0, 1.0e-4));
    CHECK_THAT (ui::GrainCloudView::dotPosition (spark, area).y, WithinAbs (0.0, 1.0e-4));
    spark.semitones = -30.0f;
    CHECK_THAT (ui::GrainCloudView::dotPosition (spark, area).y, WithinAbs (60.0, 1.0e-4));
}

TEST_CASE ("The Static trace draws bipolar targets around the centre and Level's dip below it",
           "[unit][ui][displays][static]")
{
    using Target = dsp::StaticModulator::Target;
    CHECK (juce::exactlyEqual (ui::StaticTraceView::drawnValue (Target::pan, -0.5f), -0.5f));
    CHECK (juce::exactlyEqual (ui::StaticTraceView::drawnValue (Target::filter, 2.0f), 1.0f));
    CHECK (juce::exactlyEqual (ui::StaticTraceView::drawnValue (Target::position, 0.4f), 0.4f));
    CHECK (juce::exactlyEqual (ui::StaticTraceView::drawnValue (Target::pitch, -0.4f), 0.0f));
    CHECK (juce::exactlyEqual (ui::StaticTraceView::drawnValue (Target::level, 0.75f), -0.75f));
}

TEST_CASE ("The Shape display draws the gain Shape applies", "[unit][ui][displays][shape]")
{
    dsp::ShapeModule module;
    module.prepare (PrepareSpec (kRate, 256, 2));

    for (int s = 0; s < dsp::ShapeModule::kNumShapes; ++s)
    {
        const auto shape = static_cast<params::ShapeType> (s);
        for (const auto smooth : {0.1f, 0.5f, 1.0f})
        {
            for (int i = 0; i < 40; ++i)
            {
                const auto phase = (i + 0.5) / 40.0;
                const auto expected = module.smoothedAt (shape, phase, smooth * 0.5);
                CHECK_THAT (ui::ShapeView::gainAt (shape, 1.0f, smooth, phase), WithinAbs (expected, 0.03));
            }
        }
    }

    // Depth sets how far the gain dips: at 0 the line is flat at full level.
    CHECK_THAT (ui::ShapeView::gainAt (params::ShapeType::Gate, 0.0f, 0.0f, 0.75), WithinAbs (1.0, 1.0e-6));
    CHECK_THAT (ui::ShapeView::gainAt (params::ShapeType::Gate, 0.5f, 0.0f, 0.75), WithinAbs (0.5, 1.0e-3));
}

TEST_CASE ("Duck's meter reaches across at 24 dB", "[unit][ui][displays][duck]")
{
    CHECK_THAT (ui::ReductionMeter::fraction (0.0f), WithinAbs (0.0, 1.0e-6));
    CHECK_THAT (ui::ReductionMeter::fraction (12.0f), WithinAbs (0.5, 1.0e-6));
    CHECK_THAT (ui::ReductionMeter::fraction (40.0f), WithinAbs (1.0, 1.0e-6));
    CHECK_THAT (ui::ReductionMeter::fraction (-3.0f), WithinAbs (0.0, 1.0e-6));
}

// ---- The panels
// ------------------------------------------------------------------------------------------------------

TEST_CASE ("Each module with a live display shows it and asks for its data", "[unit][ui][displays][plugin]")
{
    Fixture f;
    auto& bridge = f.processor.getUiBridge();
    auto& visualiser = f.editor->getVisualiser();

    // With the visualiser's grains hidden, only the Grains panel asks for grains.
    visualiser.chooseMenuItem (ui::VisualiserRegion::kMenuShowGrains);
    f.editor->selectModule (0);
    f.editor->updateVisuals (true, 0.0);
    CHECK ((bridge.getWanted() & UiBridge::kGrains) == 0);
    f.editor->selectModule (kGrainsPanel);
    f.editor->updateVisuals (true, 0.0);
    CHECK ((bridge.getWanted() & UiBridge::kGrains) != 0);

    // Grains are on by default: the cloud fills while the melody plays.
    f.play (0.5);
    CHECK (f.editor->getVisualModel().getGrains().count > 0);

    std::vector<juce::Component*> displays{
        &f.panel<ui::GrainsPanel> (kGrainsPanel).getCloud(), &f.panel<ui::StaticPanel> (kStaticPanel).getTrace(),
        &f.panel<ui::ShapePanel> (kShapePanel).getDisplay(), &f.panel<ui::DuckPanel> (kDuckPanel).getMeter()};
    for (auto* d : displays)
    {
        CHECK (d->isVisible());
        CHECK (d->getTitle().isNotEmpty());
        CHECK (d->getDescription().isNotEmpty());
        CHECK_FALSE (d->getWantsKeyboardFocus());
        bool childClicks = true, selfClicks = true;
        d->getInterceptsMouseClicks (selfClicks, childClicks);
        CHECK_FALSE (selfClicks);
    }

    // Every display lies inside its panel's body, at its size.
    for (auto* d : displays)
    {
        REQUIRE (d->getParentComponent() != nullptr);
        CHECK (d->getParentComponent()->getLocalBounds().contains (d->getBounds()));
        CHECK (d->getHeight() >= 28);
    }
    CHECK (f.panel<ui::ShapePanel> (kShapePanel).getDisplay().getWidth() == 380);
    CHECK (f.panel<ui::GrainsPanel> (kGrainsPanel).getCloud().getBounds().getWidth() == 120);
}

TEST_CASE ("Space's Decay reads in seconds for the chosen mode", "[unit][ui][displays][space]")
{
    Fixture f;
    f.editor->selectModule (kSpacePanel);
    auto* dial = findById (f.editor->getModulePanel (kSpacePanel), ui::paramIdString (ParamId::spaceDecay));
    REQUIRE (dial != nullptr);
    auto* knob = dynamic_cast<ui::ThunderKnob*> (dial->getParentComponent());
    REQUIRE (knob != nullptr);

    CHECK (ui::SpacePanel::decayText (2.43) == "2.4 s");
    CHECK (ui::SpacePanel::decayText (12.6) == "13 s");

    f.set (ParamId::spaceDecay, 50.0f);
    for (int m = 0; m < dsp::SpaceModule::kModes; ++m)
    {
        f.setChoice (ParamId::spaceMode, m);
        const auto expected =
            ui::SpacePanel::decayText (dsp::SpaceModule::decaySeconds (static_cast<params::SpaceMode> (m), 50.0f));
        CHECK (knob->getValueText() == expected);
    }

    // Room is shorter than Cloud at the same setting, and the text says so.
    f.setChoice (ParamId::spaceMode, 0);
    const auto cloud = knob->getValueText();
    f.setChoice (ParamId::spaceMode, static_cast<int> (params::SpaceMode::Room));
    CHECK (knob->getValueText() != cloud);
}
