// The v0.5 interface (ROADMAP T-0.5-05/06; PRD UI-GEN-003/004/006, UI.md §7–§9,
// §15): it opens at the base size and scales, every control drives its
// parameter and follows it back, one module shows at a time, and the preset
// bar and save form behave as specified. Headless: no window is shown.

#include "plugin/ThunderEditor.h"
#include "plugin/ThunderProcessor.h"
#include "plugin/ui/theme/ThunderTheme.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace thunder;
using Catch::Matchers::WithinAbs;
using params::ParamId;

namespace
{
struct TempFolder
{
    juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("ThunderEditorTests-" + juce::Uuid().toString());
    TempFolder() { folder.createDirectory(); }
    ~TempFolder() { folder.deleteRecursively(); }
};

// A processor and its editor, with presets and settings in a temporary folder.
struct Fixture
{
    TempFolder temp;
    ThunderProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editorOwner;
    ThunderEditor* editor = nullptr;

    Fixture()
    {
        processor.setSettingsFile (temp.folder.getChildFile ("settings.xml"));
        processor.getPresetManager().setUserFolder (temp.folder.getChildFile ("Presets"));
        processor.prepareToPlay (48000.0, 512);
        editorOwner.reset (processor.createEditorAndMakeActive());
        editor = dynamic_cast<ThunderEditor*> (editorOwner.get());
        REQUIRE (editor != nullptr);
    }

    float value (ParamId id)
    {
        const auto& def = params::paramDef (id);
        return processor.getValueTreeState()
            .getRawParameterValue (juce::String::fromUTF8 (def.id.data(), static_cast<int> (def.id.size())))
            ->load();
    }

    void set (ParamId id, float v)
    {
        const auto& def = params::paramDef (id);
        auto* p = processor.getValueTreeState().getParameter (
            juce::String::fromUTF8 (def.id.data(), static_cast<int> (def.id.size())));
        p->setValueNotifyingHost (p->convertTo0to1 (v));
    }
};

template<typename T> T* find (juce::Component& root, const juce::String& title)
{
    if (auto* t = dynamic_cast<T*> (&root); t != nullptr && root.getTitle() == title)
        return t;

    for (auto* child : root.getChildren())
        if (auto* found = find<T> (*child, title))
            return found;

    return nullptr;
}

ui::ThunderKnob* findKnob (juce::Component& root, const juce::String& title)
{
    if (auto* knob = dynamic_cast<ui::ThunderKnob*> (&root); knob != nullptr && knob->getSlider().getTitle() == title)
        return knob;

    for (auto* child : root.getChildren())
        if (auto* found = findKnob (*child, title))
            return found;

    return nullptr;
}
} // namespace

TEST_CASE ("The editor opens at the base size and scales as one", "[unit][ui]")
{
    // 100%, unless that wouldn't fit this machine's screen (UI.md UQ-03).
    Fixture f;
    f.editor->setSize (ui::theme::size::baseWidth, ui::theme::size::baseHeight);
    CHECK_THAT (f.editor->getScale(), WithinAbs (1.0, 1e-6));
    CHECK (f.editor->getContent().getWidth() == ui::theme::size::baseWidth);
    CHECK (f.editor->getContent().getHeight() == ui::theme::size::baseHeight);

    f.editor->setSize (ui::theme::size::baseWidth * 3 / 2, ui::theme::size::baseHeight * 3 / 2);
    CHECK_THAT (f.editor->getScale(), WithinAbs (1.5, 1e-6));

    f.editor->setSize (ui::theme::size::baseWidth * 3, ui::theme::size::baseHeight * 3);
    CHECK_THAT (f.editor->getScale(), WithinAbs (ui::theme::size::maxScale, 1e-6)); // capped at 200%
}

TEST_CASE ("One module shows at a time, chosen on the chain", "[unit][ui]")
{
    Fixture f;
    REQUIRE (f.editor->getNumModules() == 9);
    CHECK (f.editor->getModulePanel (0).isVisible());

    f.editor->getChain().clickNode (4);
    CHECK (f.editor->getSelectedModule() == 4);
    CHECK (f.editor->getModulePanel (4).getTitleText() == "Space");
    for (int i = 0; i < f.editor->getNumModules(); ++i)
        CHECK (f.editor->getModulePanel (i).isVisible() == (i == 4));

    // In processing order (PRD DEC-005).
    const char* order[] = {"Pitch", "Time", "Grains", "Static", "Space", "Tone", "Width", "Shape", "Duck"};
    for (int i = 0; i < f.editor->getNumModules(); ++i)
        CHECK (f.editor->getModulePanel (i).getTitleText() == order[i]);
}

TEST_CASE ("A chain node's dot switches its module, and the panel dims", "[unit][ui]")
{
    Fixture f;
    CHECK_THAT (f.value (ParamId::grainOn), WithinAbs (1.0, 1e-6));
    CHECK_FALSE (f.editor->getModulePanel (2).isBodyDimmed());

    f.editor->getChain().clickDot (2);
    CHECK_THAT (f.value (ParamId::grainOn), WithinAbs (0.0, 1e-6));
    CHECK (f.editor->getModulePanel (2).isBodyDimmed());

    // And back, from the host side.
    f.set (ParamId::grainOn, 1.0f);
    CHECK_FALSE (f.editor->getModulePanel (2).isBodyDimmed());
}

TEST_CASE ("Knobs drive their parameter and follow it back", "[unit][ui]")
{
    Fixture f;
    f.editor->selectModule (1);
    auto* size = findKnob (*f.editor, "Size");
    REQUIRE (size != nullptr);
    CHECK (size->getValueText() == "80 ms");

    size->getSlider().setValue (200.0, juce::sendNotificationSync);
    CHECK_THAT (f.value (ParamId::grainSize), WithinAbs (200.0, 0.01));

    f.set (ParamId::grainSize, 40.0f);
    CHECK_THAT (size->getSlider().getValue(), WithinAbs (40.0, 0.01));
    CHECK (size->getValueText() == "40 ms");

    // Double-click returns to the default.
    CHECK (size->getSlider().isDoubleClickReturnEnabled());
    CHECK_THAT (size->getSlider().getDoubleClickReturnValue(), WithinAbs (80.0, 1e-6));
}

TEST_CASE ("Knob drags cover the range in 200 px, ten times finer with Shift", "[unit][ui]")
{
    using ui::ThunderKnob;
    CHECK_THAT (ThunderKnob::dragged (0.0, 200.0, false), WithinAbs (1.0, 1e-9));
    CHECK_THAT (ThunderKnob::dragged (0.5, 20.0, false), WithinAbs (0.6, 1e-9));
    CHECK_THAT (ThunderKnob::dragged (0.5, 20.0, true), WithinAbs (0.51, 1e-9));
    CHECK_THAT (ThunderKnob::dragged (0.5, -20.0, true), WithinAbs (0.49, 1e-9));
    CHECK_THAT (ThunderKnob::dragged (0.9, 100.0, false), WithinAbs (1.0, 1e-9)); // clamped
    CHECK_THAT (ThunderKnob::dragged (0.1, -100.0, false), WithinAbs (0.0, 1e-9));
}

TEST_CASE ("The Pitch panel shows the controls of the chosen mode", "[unit][ui]")
{
    Fixture f;
    auto* mode = find<ui::SegmentedControl> (*f.editor, "Pitch Mode");
    REQUIRE (mode != nullptr);
    auto* octave = findKnob (*f.editor, "Octave");
    auto* interval = findKnob (*f.editor, "Interval");
    auto* semitones = findKnob (*f.editor, "Semitones");
    REQUIRE ((octave != nullptr && interval != nullptr && semitones != nullptr));

    CHECK (mode->getSelectedOption() == 0); // Octave by default
    CHECK (octave->isVisible());
    CHECK_FALSE (interval->isVisible());

    mode->choose (1);
    CHECK_THAT (f.value (ParamId::pitchMode), WithinAbs (1.0, 1e-6));
    CHECK (interval->isVisible());
    CHECK_FALSE (octave->isVisible());

    f.set (ParamId::pitchMode, 2.0f); // from the host
    CHECK (mode->getSelectedOption() == 2);
    CHECK (semitones->isVisible());
    CHECK_FALSE (interval->isVisible());
}

TEST_CASE ("The Space panel offers all four reverb modes", "[unit][ui]")
{
    Fixture f;
    auto* mode = find<ui::SegmentedControl> (*f.editor, "Space Mode");
    REQUIRE (mode != nullptr);
    CHECK (mode->getNumSegments() == 4);
    CHECK (mode->getSelectedOption() == 0);
    mode->choose (3);
    CHECK_THAT (f.value (ParamId::spaceMode), WithinAbs (3.0, 1e-6)); // Shimmer
}

TEST_CASE ("Every host parameter has a control", "[unit][ui]")
{
    // Each control carries its parameter's ID; every parameter must have one
    // somewhere in the editor (including panels not currently shown).
    Fixture f;

    std::function<bool (juce::Component&, const juce::String&)> has =
        [&has] (juce::Component& c, const juce::String& id)
    {
        if (c.getComponentID() == id)
            return true;
        for (auto* child : c.getChildren())
            if (has (*child, id))
                return true;
        return false;
    };

    for (const auto& def : params::kParamDefs)
    {
        const auto id = juce::String::fromUTF8 (def.id.data(), static_cast<int> (def.id.size()));
        INFO (id);
        CHECK (has (*f.editor, id));
    }
}

TEST_CASE ("The preset bar steps through presets and marks changes", "[unit][ui][presets]")
{
    Fixture f;
    auto& bar = f.editor->getTopBar().getPresetBar();
    CHECK (bar.getDisplayedName() == "Init");

    bar.stepPreset (1);
    const auto first = presets::factoryPresets().front().preset.meta.name;
    CHECK (bar.getDisplayedName() == first);

    f.set (ParamId::grainAmount, 3.0f);
    bar.refresh();
    CHECK (bar.getDisplayedName() == first + " *");

    bar.stepPreset (-1); // wraps to the last
    CHECK (bar.getDisplayedName() == f.processor.getPresetManager().getListing().entries.back().name);
}

TEST_CASE ("The save form saves, asks before replacing, and explains refusals", "[unit][ui][presets]")
{
    Fixture f;
    auto& presets = f.processor.getPresetManager();
    bool closedWithSave = false;

    {
        ui::SaveForm form (presets, true);
        form.onClose = [&closedWithSave] (bool saved) { closedWithSave = saved; };
        CHECK (form.getNameField().isEmpty());

        form.submit(); // no name
        CHECK (form.getMessage().isNotEmpty());
        CHECK_FALSE (closedWithSave);

        form.getNameField().setText ("CON", true);
        form.submit();
        CHECK (form.getMessage() == presets::describe (presets::SaveResult::invalidName));

        form.getNameField().setText ("Rainy", true);
        form.getAuthorField().setText ("Me", true);
        form.getCategoryField().setText ("Atmos", juce::dontSendNotification);
        form.getDescriptionField().setText ("Soft.", true);
        form.submit();
        CHECK (closedWithSave);
    }

    CHECK (presets.getCurrent().name == "Rainy");
    CHECK (presets.getLoadedMetadata().category == "Atmos");

    closedWithSave = false;
    {
        // Save (not Save as) prefills the loaded preset; the name exists, so it asks.
        ui::SaveForm form (presets, false);
        form.onClose = [&closedWithSave] (bool saved) { closedWithSave = saved; };
        CHECK (form.getNameField().getText() == "Rainy");
        CHECK (form.getAuthorField().getText() == "Me");
        CHECK (form.getDescriptionField().getText() == "Soft.");

        form.submit();
        CHECK (form.isAskingToReplace());
        CHECK_FALSE (closedWithSave);

        // Editing the name removes the prompt.
        form.getNameField().setText ("Rainy 2", true);
        CHECK_FALSE (form.isAskingToReplace());
        form.getNameField().setText ("Rainy", true);
        form.submit();
        REQUIRE (form.isAskingToReplace());
        form.confirmReplace();
        CHECK (closedWithSave);
    }

    // The author is remembered for the next save.
    ui::SaveForm next (presets, true);
    CHECK (next.getAuthorField().getText() == "Me");
}

TEST_CASE ("Messages appear and errors stay until dismissed", "[unit][ui]")
{
    Fixture f;
    auto& toast = f.editor->getToast();
    CHECK_FALSE (toast.isVisible());

    f.editor->showMessage ("That preset couldn't be loaded. Your sound wasn't changed.", true);
    CHECK (toast.isVisible());
    CHECK (toast.getText().contains ("couldn't be loaded"));
}

TEST_CASE ("The Output panel dims Mix in Layer Only", "[unit][ui]")
{
    Fixture f;
    auto* mix = findKnob (*f.editor, "Mix");
    REQUIRE (mix != nullptr);
    // The first "Mix" found may be Pitch's; find the Output one through its panel.
    auto* outputMode = find<ui::SegmentedControl> (*f.editor, "Output Mode");
    REQUIRE (outputMode != nullptr);
    auto* outputMix = findKnob (*outputMode->getParentComponent(), "Mix");
    REQUIRE (outputMix != nullptr);

    CHECK (juce::exactlyEqual (outputMix->getAlpha(), 1.0f));
    outputMode->choose (1);
    CHECK (outputMix->getAlpha() < 1.0f);
}

TEST_CASE ("Static shows one Rate knob at a time and its target chips switch targets", "[unit][ui][static]")
{
    Fixture f;
    f.editor->selectModule (3);
    ui::ThunderKnob* free = nullptr;
    ui::ThunderKnob* synced = nullptr;
    std::function<void (juce::Component&)> look = [&] (juce::Component& c)
    {
        if (auto* k = dynamic_cast<ui::ThunderKnob*> (&c))
        {
            if (k->getParamId() == ParamId::staticRate)
                free = k;
            if (k->getParamId() == ParamId::staticRateSynced)
                synced = k;
        }
        for (auto* child : c.getChildren())
            look (*child);
    };
    look (*f.editor);
    REQUIRE (free != nullptr);
    REQUIRE (synced != nullptr);
    CHECK (free->isVisible());
    CHECK_FALSE (synced->isVisible());

    f.set (ParamId::staticSync, 1.0f);
    CHECK_FALSE (free->isVisible());
    CHECK (synced->isVisible());
    CHECK (synced->getValueText() == "1 bar");

    auto* pitch = find<ui::ParamChip> (*f.editor, "Pitch");
    REQUIRE (pitch != nullptr);
    CHECK_FALSE (pitch->getToggleState());
    pitch->setToggleState (true, juce::sendNotificationSync); // as a click does
    CHECK_THAT (f.value (ParamId::staticTgtPitch), WithinAbs (1.0, 1e-6));
}

TEST_CASE ("Shape's picker shows each curve and chooses the shape", "[unit][ui][shape]")
{
    Fixture f;
    f.editor->selectModule (7);
    auto* picker = find<ui::SegmentedControl> (*f.editor, "Shape");
    REQUIRE (picker != nullptr);
    CHECK (picker->getNumSegments() == 6);
    CHECK (picker->getSelectedOption() == 0); // Pump
    CHECK (picker->getSegment (2).getTitle() == "Gate");
    CHECK (picker->getSegment (2).getTooltip().startsWith ("Gate - "));

    picker->choose (2);
    CHECK_THAT (f.value (ParamId::shapeType), WithinAbs (2.0, 1e-6));

    // Its picture is drawn, not its name.
    juce::Image image (juce::Image::ARGB, 64, 40, true);
    {
        juce::Graphics g (image); // drawn when it closes (Direct2D images on Windows)
        picker->getSegment (3).paintEntireComponent (g, false);
    }
    auto lit = 0;
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
            lit += image.getPixelAt (x, y).getAlpha() > 0 ? 1 : 0;
    CHECK (lit > 20);
}
