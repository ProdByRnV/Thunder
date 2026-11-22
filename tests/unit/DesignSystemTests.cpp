// The design system (ROADMAP T-0.6-02, 03, 08; PRD UI-GEN-001/002/005/007,
// UI-FX-003/004, UI-A11Y-001/002; UI.md §6, §7.1, §9, §10.3, §10.4, §14):
// scaling and the remembered size, knob text entry and reset, tooltips that say
// what every control does, accessible names, colour contrast, the Settings and
// About panels, the glow cache and the click flash. Headless: no window is shown.

#include "core/state/SessionState.h"
#include "core/state/UserSettings.h"
#include "plugin/ThunderEditor.h"
#include "plugin/ThunderProcessor.h"
#include "plugin/ui/ParamHelp.h"
#include "plugin/ui/theme/ThunderLookAndFeel.h"
#include "plugin/ui/theme/ThunderPaint.h"
#include "plugin/ui/theme/ThunderTheme.h"
#include "support/PendingControls.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <functional>
#include <typeinfo>

using namespace thunder;
using Catch::Matchers::WithinAbs;
using params::ParamId;
namespace theme = ui::theme;

namespace
{
struct TempFolder
{
    juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("ThunderDesignTests-" + juce::Uuid().toString());
    TempFolder() { folder.createDirectory(); }
    ~TempFolder() { folder.deleteRecursively(); }
};

juce::Rectangle<int> primaryWorkArea()
{
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        return display->userBounds.toNearestInt();
    return {};
}

// A processor, with presets and settings in a temporary folder, and its editor.
struct Fixture
{
    TempFolder temp;
    ThunderProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editorOwner;
    ThunderEditor* editor = nullptr;

    Fixture()
    {
        processor.setSettingsFile (settingsFile());
        processor.getPresetManager().setUserFolder (temp.folder.getChildFile ("Presets"));
        processor.prepareToPlay (48000.0, 512);
        open();
    }

    ~Fixture() { close(); }

    juce::File settingsFile() const { return temp.folder.getChildFile ("settings.xml"); }

    void open()
    {
        editorOwner.reset (processor.createEditorAndMakeActive());
        editor = dynamic_cast<ThunderEditor*> (editorOwner.get());
        REQUIRE (editor != nullptr);
    }

    void close()
    {
        if (editorOwner != nullptr)
            processor.editorBeingDeleted (editorOwner.get());
        editorOwner.reset();
        editor = nullptr;
    }

    juce::RangedAudioParameter& param (ParamId id) { return ui::parameter (processor.getValueTreeState(), id); }
    float value (ParamId id) { return param (id).convertFrom0to1 (param (id).getValue()); }
};

void visit (juce::Component& c, const std::function<void (juce::Component&)>& f)
{
    f (c);
    for (auto* child : c.getChildren())
        visit (*child, f);
}

ui::ThunderKnob* findKnob (juce::Component& root, ParamId id)
{
    ui::ThunderKnob* found = nullptr;
    visit (root,
           [&found, id] (juce::Component& c)
           {
               if (auto* k = dynamic_cast<ui::ThunderKnob*> (&c); k != nullptr && k->getParamId() == id)
                   found = k;
           });
    return found;
}

// Counts host gestures on a parameter.
struct GestureCounter final : public juce::AudioProcessorParameter::Listener
{
    explicit GestureCounter (juce::AudioProcessorParameter& p)
        : parameter (p)
    {
        parameter.addListener (this);
    }
    ~GestureCounter() override { parameter.removeListener (this); }

    void parameterValueChanged (int, float) override {}
    void parameterGestureChanged (int, bool starting) override { (starting ? begins : ends)++; }

    juce::AudioProcessorParameter& parameter;
    int begins = 0;
    int ends = 0;
};

// WCAG 2 relative luminance and contrast ratio.
double luminance (juce::Colour c)
{
    const auto channel = [] (juce::uint8 v)
    {
        const auto s = v / 255.0;
        return s <= 0.04045 ? s / 12.92 : std::pow ((s + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel (c.getRed()) + 0.7152 * channel (c.getGreen()) + 0.0722 * channel (c.getBlue());
}

double contrast (juce::Colour a, juce::Colour b)
{
    const auto la = luminance (a), lb = luminance (b);
    return (std::max (la, lb) + 0.05) / (std::min (la, lb) + 0.05);
}
} // namespace

// ---- Scaling (T-0.6-02) --------------------------------------------------------------------------------------------

TEST_CASE ("Window sizes snap to 5% steps between 75% and 200%", "[unit][ui]")
{
    CHECK_THAT (ThunderEditor::snapScale (1.0f), WithinAbs (1.0, 1e-6));
    CHECK_THAT (ThunderEditor::snapScale (1.23f), WithinAbs (1.25, 1e-6));
    CHECK_THAT (ThunderEditor::snapScale (1.07f), WithinAbs (1.05, 1e-6));
    CHECK_THAT (ThunderEditor::snapScale (0.5f), WithinAbs (0.75, 1e-6));
    CHECK_THAT (ThunderEditor::snapScale (3.0f), WithinAbs (2.0, 1e-6));

    Fixture f;
    auto* constrainer = f.editor->getConstrainer();
    REQUIRE (constrainer != nullptr);

    // Dragging the corner to 123% lands on 125%, with the aspect ratio kept.
    constrainer->setBoundsForComponent (f.editor,
                                        juce::Rectangle<int> (juce::roundToInt (theme::size::baseWidth * 1.23f),
                                                              juce::roundToInt (theme::size::baseHeight * 1.23f)),
                                        false, false, true, true);
    CHECK_THAT (f.editor->getScale(), WithinAbs (1.25, 1e-3));
    CHECK (f.editor->getWidth() == juce::roundToInt (theme::size::baseWidth * 1.25f));
    CHECK (f.editor->getHeight() == juce::roundToInt (theme::size::baseHeight * 1.25f));
}

TEST_CASE ("A window that wouldn't fit the screen opens smaller", "[unit][ui]")
{
    // UI.md UQ-03: room is left for the host's window frame (32 × 64).
    CHECK_THAT (ThunderEditor::fittingScale (1.0f, {0, 0, 1920, 1032}), WithinAbs (1.0, 1e-6)); // 1080p at 100%
    CHECK_THAT (ThunderEditor::fittingScale (1.0f, {0, 0, 1280, 672}), WithinAbs (0.85, 1e-6)); // 13" 1080p at 150%
    CHECK_THAT (ThunderEditor::fittingScale (1.0f, {0, 0, 1366, 720}), WithinAbs (0.95, 1e-6)); // 1366 × 768
    CHECK_THAT (ThunderEditor::fittingScale (1.0f, {0, 0, 1470, 900}), WithinAbs (1.0, 1e-6));  // 13" MacBook Air
    CHECK_THAT (ThunderEditor::fittingScale (2.0f, {0, 0, 2560, 1400}), WithinAbs (1.95, 1e-6));
    CHECK_THAT (ThunderEditor::fittingScale (1.0f, {0, 0, 640, 480}), WithinAbs (0.75, 1e-6)); // never below 75%
    CHECK_THAT (ThunderEditor::fittingScale (1.5f, {}), WithinAbs (1.5, 1e-6));                // no display
}

TEST_CASE ("The window size is remembered for the project and for new ones", "[unit][ui]")
{
    Fixture f;
    CHECK_THAT (f.editor->getScale(), WithinAbs (ThunderEditor::fittingScale (1.0f, primaryWorkArea()), 1e-6));
    CHECK_THAT (f.processor.getSessionState().uiScale, WithinAbs (0.0, 1e-6)); // opening doesn't set it

    // From Settings: this project, and the default for others.
    f.editor->setScale (1.5f);
    CHECK_THAT (f.editor->getScale(), WithinAbs (1.5, 1e-6));
    CHECK_THAT (f.processor.getSessionState().uiScale, WithinAbs (1.5, 1e-6));
    CHECK_THAT (state::UserSettings::load (f.settingsFile()).uiScale, WithinAbs (1.5, 1e-6));

    // Reopening uses the project's size (made to fit this screen).
    f.close();
    f.open();
    CHECK_THAT (f.editor->getScale(), WithinAbs (ThunderEditor::fittingScale (1.5f, primaryWorkArea()), 1e-6));

    // The session carries it through the host's save and reload.
    juce::MemoryBlock saved;
    f.processor.getStateInformation (saved);
    ThunderProcessor reloaded;
    reloaded.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    CHECK_THAT (reloaded.getSessionState().uiScale, WithinAbs (1.5, 1e-6));
}

TEST_CASE ("Scale and tooltip settings survive a round trip and reject nonsense", "[unit][state]")
{
    TempFolder temp;
    const auto file = temp.folder.getChildFile ("settings.xml");

    state::UserSettings s;
    CHECK (s.showTooltips);
    s.uiScale = 1.25f;
    s.showTooltips = false;
    REQUIRE (s.save (file));
    const auto loaded = state::UserSettings::load (file);
    CHECK_THAT (loaded.uiScale, WithinAbs (1.25, 1e-6));
    CHECK_FALSE (loaded.showTooltips);

    state::SessionState session;
    session.uiScale = 1.75f;
    CHECK (state::SessionState::fromValueTree (session.toValueTree()) == session);

    auto tree = session.toValueTree();
    tree.setProperty ("uiScale", 9.0, nullptr);
    CHECK_THAT (state::SessionState::fromValueTree (tree).uiScale, WithinAbs (0.0, 1e-6));
    tree.removeProperty ("uiScale", nullptr);
    CHECK_THAT (state::SessionState::fromValueTree (tree).uiScale, WithinAbs (0.0, 1e-6));
}

TEST_CASE ("The editor draws through OpenGL on Windows and macOS", "[unit][ui]")
{
    // Windows and macOS: the context is set up and starts once the window is
    // on screen (headless it stays idle). Linux: JUCE's own renderer (DEC-048).
    Fixture f;
    CHECK (f.editor->usesOpenGL() == ThunderEditor::useOpenGL);
}

// ---- Components (T-0.6-03) -----------------------------------------------------------------------------------------

TEST_CASE ("Typing a value sets the knob in one host gesture; nonsense changes nothing", "[unit][ui]")
{
    Fixture f;
    f.editor->selectModule (0);
    auto* fine = findKnob (*f.editor, ParamId::pitchFine);
    REQUIRE (fine != nullptr);
    GestureCounter gestures (f.param (ParamId::pitchFine));

    CHECK (fine->enterValue ("+25"));
    CHECK_THAT (f.value (ParamId::pitchFine), WithinAbs (25.0, 1e-3));
    CHECK (gestures.begins == 1);
    CHECK (gestures.ends == 1);
    CHECK (fine->getValueText().contains ("25"));

    CHECK (fine->enterValue ("-12 cents"));
    CHECK_THAT (f.value (ParamId::pitchFine), WithinAbs (-12.0, 1e-3));

    CHECK (fine->enterValue ("500")); // clamped to the range
    CHECK_THAT (f.value (ParamId::pitchFine), WithinAbs (100.0, 1e-3));

    CHECK_FALSE (fine->enterValue ("loud"));
    CHECK_THAT (f.value (ParamId::pitchFine), WithinAbs (100.0, 1e-3));

    fine->resetToDefault();
    CHECK_THAT (f.value (ParamId::pitchFine), WithinAbs (0.0, 1e-3));
    CHECK (gestures.begins == gestures.ends);
}

TEST_CASE ("A module's reset puts its controls back and leaves it switched on", "[unit][ui]")
{
    Fixture f;
    f.param (ParamId::grainSize).setValueNotifyingHost (f.param (ParamId::grainSize).convertTo0to1 (300.0f));
    f.param (ParamId::grainReverse).setValueNotifyingHost (0.5f);
    f.param (ParamId::spaceMix).setValueNotifyingHost (1.0f); // another module: untouched
    GestureCounter gestures (f.param (ParamId::grainSize));

    f.editor->getModulePanel (2).resetToDefaults();
    CHECK_THAT (f.value (ParamId::grainSize), WithinAbs (80.0, 0.01));
    CHECK_THAT (f.value (ParamId::grainReverse), WithinAbs (0.0, 1e-3));
    CHECK_THAT (f.value (ParamId::grainOn), WithinAbs (1.0, 1e-6));
    CHECK_THAT (f.value (ParamId::spaceMix), WithinAbs (100.0, 1e-3));
    CHECK (gestures.begins == 1);
    CHECK (gestures.ends == 1);
}

TEST_CASE ("The knob arc glows with its value, a little more on hover", "[unit][ui]")
{
    using ui::ThunderKnob;
    CHECK_THAT (ThunderKnob::arcGlow (0.0, false, false), WithinAbs (0.0, 1e-6));
    CHECK_THAT (ThunderKnob::arcGlow (1.0, false, false), WithinAbs (theme::glow::medium.opacity, 1e-6));
    CHECK_THAT (ThunderKnob::arcGlow (0.5, false, true),
                WithinAbs (theme::glow::medium.opacity * 0.5f + theme::glow::hoverBoost, 1e-6));
    CHECK_THAT (ThunderKnob::arcGlow (0.5, true, false), WithinAbs (0.0, 1e-6)); // bipolar at the centre
    CHECK_THAT (ThunderKnob::arcGlow (0.0, true, false), WithinAbs (theme::glow::medium.opacity, 1e-6));
}

TEST_CASE ("Glows are rendered once and reused until the shape or scale changes", "[unit][ui]")
{
    ui::paint::GlowCache cache;
    juce::Path shape;
    shape.addEllipse (10.0f, 10.0f, 40.0f, 40.0f);

    juce::Image target (juce::Image::ARGB, 120, 120, true, juce::SoftwareImageType());
    {
        juce::Graphics g (target);
        cache.draw (g, shape, theme::colour::glowPrimary, theme::glow::medium, 1.0f);
        cache.draw (g, shape, theme::colour::glowPrimary, theme::glow::medium, 0.5f);
        CHECK (cache.getRenderCount() == 1);

        g.addTransform (juce::AffineTransform::scale (2.0f));
        cache.draw (g, shape, theme::colour::glowPrimary, theme::glow::medium, 1.0f);
        CHECK (cache.getRenderCount() == 2);
    }

    // The glow spreads beyond the shape: a pixel just outside it is tinted.
    CHECK (target.getPixelAt (8, 30).getAlpha() > 0);
}

TEST_CASE ("Switching a module on flashes its switch; opening the editor doesn't", "[unit][ui]")
{
    Fixture f;
    auto* toggle = dynamic_cast<ui::ThunderToggle*> (
        f.editor->getModulePanel (2).findChildWithID (ui::paramIdString (ParamId::grainOn)));
    REQUIRE (toggle != nullptr);
    CHECK (juce::exactlyEqual (toggle->getFlashLevel(), 0.0f));

    f.param (ParamId::grainOn).setValueNotifyingHost (0.0f);
    CHECK (juce::exactlyEqual (toggle->getFlashLevel(), 0.0f)); // turning off: no flash
    f.param (ParamId::grainOn).setValueNotifyingHost (1.0f);
    CHECK (toggle->getFlashLevel() > 0.5f);
}

TEST_CASE ("Every control says what it does", "[unit][ui]")
{
    // PRD UI-GEN-005: a description for every parameter, shown in the tooltip
    // of every control that sets it.
    for (const auto& def : params::kParamDefs)
    {
        INFO (ui::help::name (def.param));
        CHECK (ui::help::describe (def.param).length() > 10);
        CHECK (ui::help::describe (def.param) != ui::help::name (def.param));
    }

    Fixture f;
    int controls = 0;
    visit (f.editor->getContent(),
           [&controls] (juce::Component& c)
           {
               const auto id = params::findParam (c.getComponentID().toStdString());
               if (!id.has_value())
                   return;

               ++controls;
               INFO (c.getComponentID());
               auto* tip = dynamic_cast<juce::TooltipClient*> (&c);
               REQUIRE (tip != nullptr);
               CHECK (tip->getTooltip().contains (ui::help::describe (*id)));
               CHECK (c.getDescription() == ui::help::describe (*id));
           });
    CHECK (controls >= static_cast<int> (params::kParamDefs.size() - test::kControlsArrivingLater.size()));

    // Numbers also show their value and range.
    auto* decay = findKnob (*f.editor, ParamId::spaceDecay);
    REQUIRE (decay != nullptr);
    const auto tip = decay->getSlider().getTooltip();
    CHECK (tip.contains (decay->getValueText()));
    CHECK (tip.contains (" to "));
}

TEST_CASE ("Tooltips set the name in bold and the value line in the mono font", "[unit][ui]")
{
    const auto text = ui::ThunderLookAndFeel::tooltipText ("Decay: How long the reverb rings.\n50%  (0% to 100%)");
    REQUIRE (text.getNumAttributes() == 3);
    CHECK (text.getAttribute (0).range.getLength() == juce::String ("Decay").length());
    CHECK (text.getAttribute (1).font.getTypefacePtr() != text.getAttribute (2).font.getTypefacePtr());
}

// ---- Accessibility (T-0.6-08) --------------------------------------------------------------------------------------

TEST_CASE ("Every control has an accessible name", "[unit][ui][a11y]")
{
    // PRD UI-A11Y-001: what a screen reader announces. Buttons may use their
    // text; everything else needs a title. Hidden module panels count too.
    Fixture f;
    f.editor->openSettings();
    f.editor->openAbout();

    int checked = 0;
    visit (f.editor->getContent(),
           [&checked] (juce::Component& c)
           {
               if (!c.isAccessible() || c.getAlpha() <= 0.0f)
                   return;

               const auto* button = dynamic_cast<juce::Button*> (&c);
               const auto interactive = button != nullptr || dynamic_cast<juce::Slider*> (&c) != nullptr ||
                                        dynamic_cast<juce::ComboBox*> (&c) != nullptr;
               if (!interactive)
                   return;

               ++checked;
               INFO (c.getComponentID() << " / " << typeid (c).name());
               CHECK ((c.getTitle().isNotEmpty() || (button != nullptr && button->getButtonText().isNotEmpty())));
           });
    CHECK (checked > 50);

    // Knobs read their value the way the host shows it.
    auto* size = findKnob (*f.editor, ParamId::grainSize);
    REQUIRE (size != nullptr);
    CHECK (size->getSlider().getTextFromValue (size->getSlider().getValue()) == size->getValueText());
}

TEST_CASE ("Text colours meet WCAG contrast on every surface they're used on", "[unit][ui][a11y]")
{
    // PRD UI-A11Y-002; UI.md §3.4: labels AA (4.5:1), values AAA (7:1).
    using namespace theme::colour;
    for (const auto surface : {bgDeep, bgBase, bgPanel, bgRaised, bgHover})
    {
        INFO ("surface " << surface.toDisplayString (false));
        CHECK (contrast (textPrimary, surface) >= 7.0);
        CHECK (contrast (textSecondary, surface) >= 4.5);
    }

    CHECK (contrast (textOnAccent, accentPrimary) >= 7.0); // selected segments, primary buttons
    CHECK (contrast (stateError, bgRaised) >= 4.5);        // danger buttons, failed captures
    CHECK (contrast (stateSuccess, bgRaised) >= 4.5);
    CHECK (contrast (stateWarning, bgPanel) >= 4.5);
    CHECK (contrast (accentPrimary, bgPanel) >= 4.5); // selected and on states
}

// ---- Settings and About (UI.md §10.3, §10.4) ----------------------------------------------------------------------

TEST_CASE ("Settings change the size, tooltips, capture format, folder and preset loading", "[unit][ui]")
{
    Fixture f;
    f.editor->openSettings();
    auto* settings = f.editor->getSettingsPanel();
    REQUIRE (settings != nullptr);
    CHECK (settings->popoverBounds().getRight() <= f.editor->getContent().getWidth());

    settings->getScaleBox().setSelectedItemIndex (3, juce::sendNotificationSync); // 125%
    CHECK_THAT (f.editor->getScale(), WithinAbs (1.25, 1e-6));
    CHECK_THAT (state::UserSettings::load (f.settingsFile()).uiScale, WithinAbs (1.25, 1e-6));

    CHECK (f.editor->areTooltipsShown());
    settings->setShowTooltips (false);
    CHECK_FALSE (f.editor->areTooltipsShown());
    CHECK_FALSE (state::UserSettings::load (f.settingsFile()).showTooltips);

    settings->setCaptureFormat (true);
    CHECK (state::UserSettings::load (f.settingsFile()).captureFormat == capture::CaptureFormat::Float32);

    const auto folder = f.temp.folder.getChildFile ("Mine");
    folder.createDirectory();
    settings->setCapturesFolder (folder);
    CHECK (f.processor.getCapturesFolder() == folder);
    CHECK (settings->getFolderText().endsWith ("Mine")); // the path, shortened from the start
    settings->setCapturesFolder ({});
    CHECK (f.processor.getCapturesFolder() == state::UserSettings::defaultCapturesFolder());

    settings->setPreserveSession (false);
    CHECK_FALSE (f.processor.getPresetManager().getPreserveSession());

    // A new editor starts without tooltips now.
    f.close();
    f.open();
    CHECK_FALSE (f.editor->areTooltipsShown());
}

TEST_CASE ("The About panel shows the version and closes", "[unit][ui]")
{
    CHECK (ui::AboutPanel::versionLine().startsWith ("Version " + juce::String (THUNDER_VERSION_STRING)));
    CHECK (ui::AboutPanel::versionLine().contains ("built"));

    ui::AboutPanel about;
    about.setSize (theme::size::baseWidth, theme::size::baseHeight);
    bool closed = false;
    about.onClose = [&closed] { closed = true; };
    CHECK (about.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
    CHECK (closed);
}
