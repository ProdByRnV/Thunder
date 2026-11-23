// Calm by choice (ROADMAP T-0.7-04…06; PRD UI-FX-002, UI-FX-004, UI-FX-005,
// UI-FX-006; UI.md §10.3, §12.3, §12.5): Static Amount sets how readily
// lightning strikes, never more than 3 flashes a second and never outside the
// well, Reduce Motion stops every decorative animation, and the frame rate
// follows Settings. Headless: no window is shown.

#include "support/TestHost.h"

#include "core/state/UserSettings.h"
#include "plugin/ThunderEditor.h"
#include "plugin/ThunderProcessor.h"
#include "plugin/ui/Motion.h"
#include "plugin/ui/ParamUi.h"
#include "plugin/ui/theme/ThunderTheme.h"
#include "plugin/ui/visualiser/Lightning.h"
#include "plugin/ui/visualiser/LightningView.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

using namespace thunder;
using params::ParamId;
namespace tokens = thunder::ui::theme::lightning;

namespace
{
constexpr double kRate = 48000.0;
constexpr double kFrame = 64.0 / kRate;

struct TempFolder
{
    juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("ThunderMotionTests-" + juce::Uuid().toString());
    TempFolder() { folder.createDirectory(); }
    ~TempFolder() { folder.deleteRecursively(); }
    juce::File settings() const { return folder.getChildFile ("settings.xml"); }
};

// An editor on a processor with its own settings file.
struct Fixture
{
    explicit Fixture (const std::function<void (state::UserSettings&)>& prepare = {})
    {
        // Reduce Motion chosen (off unless `prepare` says otherwise), so the
        // machine's animation setting doesn't decide it.
        state::UserSettings s;
        s.reduceMotionChosen = true;
        if (prepare)
            prepare (s);
        s.save (temp.settings());
        processor.setSettingsFile (temp.settings());
        processor.getPresetManager().setUserFolder (temp.folder.getChildFile ("Presets"));
        processor.prepareToPlay (kRate, 512);
        processor.setPlayHead (&head);
        owner.reset (processor.createEditorAndMakeActive());
        editor = dynamic_cast<ThunderEditor*> (owner.get());
        REQUIRE (editor != nullptr);
    }

    ~Fixture()
    {
        processor.setPlayHead (nullptr);
        processor.editorBeingDeleted (owner.get());
        owner.reset();
        ui::reduceMotion::setOn (false); // the flag is process-wide
    }

    void set (ParamId id, float value)
    {
        auto& p = ui::parameter (processor.getValueTreeState(), id);
        p.setValueNotifyingHost (p.convertTo0to1 (value));
    }

    std::uint64_t playAndCountStrikes (double seconds)
    {
        auto& lightning = editor->getVisualiser().getLightning();
        const auto before = lightning.getStrikeCount();
        editor->updateVisuals (true, 0.0);
        test::playMelody (processor, head, seconds, kRate, 512,
                          [this] { editor->updateVisuals (true, 512.0 / kRate); });
        return lightning.getStrikeCount() - before;
    }

    TempFolder temp;
    ThunderProcessor processor;
    test::TestPlayHead head;
    std::unique_ptr<juce::AudioProcessorEditor> owner;
    ThunderEditor* editor = nullptr;
};

// Notes rising `riseDb` over a held level, every half second, for `notes` notes.
int softNotes (ui::lightning::StrikeDetector& d, float riseDb, int notes)
{
    auto strikes = 0;
    const auto base = 0.05f;
    const auto peak = base * juce::Decibels::decibelsToGain (riseDb);
    for (double t = 0.0; t < 1.0; t += kFrame)
        d.addFrame (base, base, kFrame);
    for (int n = 0; n < notes; ++n)
    {
        for (double t = 0.0; t < 0.05; t += kFrame)
            strikes += d.addFrame (peak, peak, kFrame) ? 1 : 0;
        for (double t = 0.0; t < 0.45; t += kFrame)
            strikes += d.addFrame (base, base, kFrame) ? 1 : 0;
    }
    return strikes;
}
} // namespace

// ---- Static Amount drives the strikes (T-0.7-04)
// ----------------------------------------------------------------------

TEST_CASE ("Sensitivity sets how strong a transient must be to strike", "[unit][lightning][motion]")
{
    ui::lightning::StrikeDetector strong, sensitive;
    strong.setSensitivity (0.0f);
    sensitive.setSensitivity (1.0f);

    // Soft 5 dB rises: only the sensitive detector strikes on them.
    CHECK (softNotes (strong, 5.0f, 10) == 0);
    CHECK (softNotes (sensitive, 5.0f, 10) >= 8);

    // Strong 15 dB hits strike at any sensitivity (Static off too).
    ui::lightning::StrikeDetector off;
    off.setSensitivity (0.0f);
    CHECK (softNotes (off, 15.0f, 6) >= 5);

    // The least rise: 6 dB at 0, 3 dB at 1.
    ui::lightning::StrikeDetector fresh;
    CHECK (juce::exactlyEqual (fresh.getThresholdDb(), tokens::onsetMinStrongDb));
    fresh.setSensitivity (1.0f);
    CHECK (juce::exactlyEqual (fresh.getThresholdDb(), tokens::onsetMinSensitiveDb));
    fresh.setSensitivity (5.0f); // clamped
    CHECK (juce::exactlyEqual (fresh.getSensitivity(), 1.0f));
}

TEST_CASE ("With Static at full Amount the melody strikes more often than with Static off",
           "[unit][lightning][motion][plugin]")
{
    Fixture off;
    const auto calm = off.playAndCountStrikes (8.0);

    Fixture full;
    full.set (ParamId::staticOn, 1.0f);
    full.set (ParamId::staticAmount, 100.0f);
    full.editor->updateVisuals (true, 0.0);
    const auto stormy = full.playAndCountStrikes (8.0);

    CHECK (stormy > calm);
    CHECK (stormy <= 8u * 3u); // never past the flash limit
}

// ---- Flash limits (T-0.7-05)
// ------------------------------------------------------------------------------------------

TEST_CASE ("The flash limiter never lets 4 flashes into any second", "[unit][lightning][motion]")
{
    ui::lightning::FlashLimiter limiter;
    std::vector<double> accepted;
    juce::Random random (7);
    double t = 0.0;
    for (int i = 0; i < 400; ++i)
    {
        t += random.nextDouble() * 0.2;
        // Some flashes are booked ahead (a re-flicker), as the view does.
        const auto at = t + (random.nextBool() ? 0.09 : 0.0);
        if (limiter.tryAdd (at))
            accepted.push_back (at);
    }
    REQUIRE (accepted.size() > 50);

    std::sort (accepted.begin(), accepted.end());
    for (std::size_t i = 0; i < accepted.size(); ++i)
    {
        const auto inWindow = std::count_if (accepted.begin(), accepted.end(),
                                             [&] (double x) { return x >= accepted[i] && x < accepted[i] + 1.0; });
        CHECK (inWindow <= 3);
    }
}

TEST_CASE ("Strikes that would flash too often are dropped, not queued", "[unit][lightning][motion][ui]")
{
    ui::LightningView view;
    view.setBounds (0, 0, 800, 230);

    // A strike asked for every 100 ms for 5 s.
    for (int i = 0; i < 50; ++i)
    {
        view.strike (0.5f, 1.0f);
        view.advance (0.1);
    }
    CHECK (view.getDroppedCount() > 0);
    CHECK (view.getStrikeCount() <= 16u); // 3 a second at most (and fewer when they flicker)
    CHECK (view.getFlashes().busiestWindow() <= ui::lightning::FlashLimiter::kMaxFlashes);
    CHECK (view.getNumBolts() <= tokens::maxBolts);
}

TEST_CASE ("A strike brightens only the visualiser well, by 6% at most", "[unit][lightning][motion][plugin]")
{
    Fixture f;
    auto& region = f.editor->getVisualiser();
    auto& lightning = region.getLightning();

    // The lightning layer is the well, inside the visualiser region.
    CHECK (region.getLocalBounds().contains (lightning.getBounds()));
    CHECK (lightning.getWidth() < f.editor->getContent().getWidth());

    // The ambient fill, away from the bolt: at most 6% white.
    lightning.strike (0.5f, 1.0f);
    juce::Image image (juce::Image::ARGB, lightning.getWidth(), lightning.getHeight(), true, juce::SoftwareImageType());
    {
        juce::Graphics g (image);
        lightning.paint (g);
    }
    const auto alpha = image.getPixelAt (2, lightning.getHeight() - 3).getFloatAlpha();
    CHECK (alpha > 0.0f);
    CHECK (alpha <= tokens::ambientLevel + 1.0f / 255.0f);
}

// ---- Reduce Motion and the frame rate (T-0.7-06)
// ------------------------------------------------------------------------------

TEST_CASE ("Reduce Motion and the frame rate are saved as global settings", "[unit][motion][state]")
{
    TempFolder temp;
    state::UserSettings fresh;
    fresh.save (temp.settings());
    auto loaded = state::UserSettings::load (temp.settings());
    CHECK_FALSE (loaded.reduceMotionChosen); // follows the system until chosen
    CHECK (loaded.frameRate == 60);

    loaded.reduceMotion = true;
    loaded.reduceMotionChosen = true;
    loaded.frameRate = 30;
    loaded.save (temp.settings());
    const auto again = state::UserSettings::load (temp.settings());
    CHECK (again == loaded);
    CHECK (ui::reduceMotion::wanted (again));

    again.save (temp.settings());
    auto off = again;
    off.reduceMotion = false;
    CHECK_FALSE (ui::reduceMotion::wanted (off));

    // A frame rate other than 30 reads as 60.
    juce::PropertiesFile odd (temp.settings(), {});
    odd.setValue ("frameRate", 45);
    odd.save();
    CHECK (state::UserSettings::load (temp.settings()).frameRate == 60);
}

TEST_CASE ("Under Reduce Motion nothing decorative moves; information still does", "[unit][motion][plugin]")
{
    Fixture f (
        [] (state::UserSettings& s)
        {
            s.reduceMotion = true;
            s.reduceMotionChosen = true;
        });
    CHECK (ui::reduceMotion::isOn());

    // Static at full Amount would strike often; nothing strikes.
    f.set (ParamId::staticOn, 1.0f);
    f.set (ParamId::staticAmount, 100.0f);
    CHECK (f.playAndCountStrikes (4.0) == 0u);
    CHECK (f.editor->getVisualiser().getLightning().getNumBolts() == 0);

    // The waveform and meters still updated.
    CHECK (f.editor->getVisualModel().getHistory().size() > 0);

    // Switching a module on doesn't flash.
    auto* toggle = dynamic_cast<ui::ThunderToggle*> (
        f.editor->getModulePanel (3).findChildWithID (ui::paramIdString (ParamId::staticOn)));
    REQUIRE (toggle != nullptr);
    f.set (ParamId::staticOn, 0.0f);
    f.set (ParamId::staticOn, 1.0f);
    CHECK (juce::exactlyEqual (toggle->getFlashLevel(), 0.0f));
}

TEST_CASE ("Turning Reduce Motion on in Settings clears the lightning at once", "[unit][motion][plugin]")
{
    Fixture f;
    auto& lightning = f.editor->getVisualiser().getLightning();
    lightning.strike (0.5f, 1.0f);
    REQUIRE (lightning.getNumBolts() == 1);

    f.editor->openSettings();
    REQUIRE (f.editor->getSettingsPanel() != nullptr);
    f.editor->getSettingsPanel()->setReduceMotion (true);
    CHECK (ui::reduceMotion::isOn());
    CHECK (lightning.getNumBolts() == 0);
    CHECK (state::UserSettings::load (f.temp.settings()).reduceMotionChosen);

    f.editor->getSettingsPanel()->setReduceMotion (false);
    CHECK_FALSE (ui::reduceMotion::isOn());
}

TEST_CASE ("The frame rate follows Settings, and Eco always draws at 30", "[unit][motion][plugin]")
{
    Fixture f;
    CHECK (f.editor->getFrameRate() == 60);

    f.editor->openSettings();
    f.editor->getSettingsPanel()->setFrameRate (30);
    CHECK (f.editor->getFrameRate() == 30);
    f.editor->getSettingsPanel()->setFrameRate (60);
    CHECK (f.editor->getFrameRate() == 60);

    f.editor->getSettingsPanel()->setQuality (params::Quality::Eco);
    CHECK (f.editor->getFrameRate() == 30);
}
