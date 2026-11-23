// Lightning (ROADMAP T-0.7-01…03; PRD UI-FX-001, UI-FX-002; UI.md §12.2–§12.5):
// the bolt generator's geometry, the strike timeline, the transient detector
// and brightness, the view's bolts, and lightning while the editor plays.
// Headless: no window is shown.

#include "support/TestHost.h"
#include "support/TestSettings.h"

#include "plugin/ThunderEditor.h"
#include "plugin/ThunderProcessor.h"
#include "plugin/ui/ParamUi.h"
#include "plugin/ui/theme/ThunderTheme.h"
#include "plugin/ui/visualiser/Lightning.h"
#include "plugin/ui/visualiser/LightningView.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <vector>

using namespace thunder;
using namespace thunder::ui::lightning;
using Catch::Matchers::WithinAbs;
namespace tokens = thunder::ui::theme::lightning;

namespace
{
constexpr double kFrame = 64.0 / 48000.0; // one visualiser frame

// Feeds `seconds` of frames at a constant layer peak; returns the strikes.
int feed (StrikeDetector& d, float layerPeak, double seconds, float outputPeak = -1.0f)
{
    auto strikes = 0;
    for (double t = 0.0; t < seconds; t += kFrame)
        strikes += d.addFrame (layerPeak, outputPeak < 0.0f ? layerPeak : outputPeak, kFrame) ? 1 : 0;
    return strikes;
}

float distanceFromLine (juce::Point<float> p, juce::Point<float> a, juce::Point<float> b)
{
    const auto ab = b - a;
    const auto length = a.getDistanceFrom (b);
    return std::abs (ab.x * (a.y - p.y) - ab.y * (a.x - p.x)) / length;
}
} // namespace

// ---- The bolt generator (T-0.7-01)
// -----------------------------------------------------------------------------------

TEST_CASE ("A bolt runs exactly from its start to its end, subdivided 5 or 6 times", "[unit][lightning]")
{
    const juce::Point<float> start (300.0f, 0.0f), end (380.0f, 200.0f);
    for (std::uint64_t seed = 1; seed <= 50; ++seed)
    {
        const auto bolt = generateBolt (start, end, seed);
        REQUIRE_FALSE (bolt.strands.empty());
        const auto& main = bolt.strands.front();
        CHECK (main.points.front() == start);
        CHECK (main.points.back() == end);
        CHECK (main.points.size() == (1u << tokens::depthLong) + 1u); // long: 6 levels
        CHECK (juce::exactlyEqual (main.brightness, 1.0f));

        for (std::size_t i = 1; i < bolt.strands.size(); ++i)
        {
            const auto& branch = bolt.strands[i];
            CHECK (juce::exactlyEqual (branch.brightness, tokens::branchBrightness));
            CHECK (branch.points.size() == (1u << (tokens::depthLong - tokens::branchDepthDrop)) + 1u);
            // A branch starts on the main strand.
            CHECK (std::find (main.points.begin(), main.points.end(), branch.points.front()) != main.points.end());
        }

        for (const auto& s : bolt.strands)
            for (const auto& p : s.points)
            {
                CHECK (std::isfinite (p.x));
                CHECK (std::isfinite (p.y));
            }
    }

    // A short bolt gets 5 levels.
    const auto shortBolt = generateBolt ({0.0f, 0.0f}, {0.0f, 100.0f}, 7);
    CHECK (shortBolt.strands.front().points.size() == (1u << tokens::depthShort) + 1u);
}

TEST_CASE ("The same seed gives the same bolt; another seed a different one", "[unit][lightning]")
{
    const juce::Point<float> start (100.0f, 0.0f), end (200.0f, 180.0f);
    const auto a = generateBolt (start, end, 42);
    const auto b = generateBolt (start, end, 42);
    const auto c = generateBolt (start, end, 43);

    REQUIRE (a.strands.size() == b.strands.size());
    for (std::size_t s = 0; s < a.strands.size(); ++s)
        CHECK (a.strands[s].points == b.strands[s].points);
    CHECK (a.strands.front().points != c.strands.front().points);
}

TEST_CASE ("Midpoint displacement stays within its shrinking reach", "[unit][lightning]")
{
    // Level k moves points by at most length × 0.35 × 0.55^k, so no point can be
    // further from the straight line than the sum over the levels.
    const juce::Point<float> start (0.0f, 0.0f), end (0.0f, 400.0f);
    auto limit = 0.0f;
    for (int k = 0; k < tokens::depthLong; ++k)
        limit += 400.0f * tokens::displacement * std::pow (tokens::jitterDecay, static_cast<float> (k));

    auto largest = 0.0f;
    for (std::uint64_t seed = 1; seed <= 200; ++seed)
    {
        const auto bolt = generateBolt (start, end, seed);
        for (const auto& p : bolt.strands.front().points)
            largest = std::max (largest, distanceFromLine (p, start, end));
    }
    CHECK (largest <= limit + 1.0e-3f);
    CHECK (largest > 0.2f * limit); // it does move

    // Kept inside a given area, branches too.
    const juce::Rectangle<float> within (-20.0f, 0.0f, 40.0f, 400.0f);
    for (std::uint64_t seed = 1; seed <= 100; ++seed)
    {
        const auto bolt = generateBolt (start, end, seed, within);
        for (const auto& s : bolt.strands)
            for (const auto& p : s.points)
                CHECK (within.expanded (1.0e-3f).contains (p.x, p.y));
    }
}

TEST_CASE ("Branches start at about 12% of the main strand's subdivisions", "[unit][lightning]")
{
    // 63 midpoints on a 6-level strand × 12% ≈ 7.6 branches on average.
    auto total = 0.0;
    constexpr int bolts = 2000;
    for (std::uint64_t seed = 1; seed <= bolts; ++seed)
        total += static_cast<double> (generateBolt ({0.0f, 0.0f}, {50.0f, 220.0f}, seed).strands.size() - 1);
    const auto expected = ((1 << tokens::depthLong) - 1) * static_cast<double> (tokens::branchChance);
    CHECK_THAT (total / bolts, WithinAbs (expected, 0.4));
}

// ---- The strike timeline (T-0.7-02)
// --------------------------------------------------------------------------------------

TEST_CASE ("A strike holds 60 ms, then fades out over 250 ms; a flicker re-flashes at 40%", "[unit][lightning]")
{
    CHECK (juce::exactlyEqual (strikeLevel (0.0, false), 1.0f));
    CHECK (juce::exactlyEqual (strikeLevel (0.059, false), 1.0f));
    CHECK_THAT (strikeLevel (0.060 + 0.125, false), WithinAbs (0.25, 1.0e-4)); // ease-out half way
    CHECK (strikeLevel (0.2, false) > strikeLevel (0.25, false));
    CHECK_THAT (strikeLength (false), WithinAbs (0.31, 1.0e-9));
    CHECK (juce::exactlyEqual (strikeLevel (0.31, false), 0.0f));
    CHECK (juce::exactlyEqual (strikeLevel (-0.01, false), 0.0f));

    // With the flicker: dark 60–90 ms, 40% for 90–120 ms, then the fade.
    CHECK (juce::exactlyEqual (strikeLevel (0.075, true), 0.0f));
    CHECK (juce::exactlyEqual (strikeLevel (0.1, true), tokens::flickerLevel));
    CHECK (strikeLevel (0.13, true) < tokens::flickerLevel);
    CHECK_THAT (strikeLength (true), WithinAbs (0.37, 1.0e-9));
    CHECK (juce::exactlyEqual (strikeLevel (0.37, true), 0.0f));

    // The well's ambient flash lasts 80 ms.
    CHECK (juce::exactlyEqual (ambientLevel (0.0), 1.0f));
    CHECK (juce::exactlyEqual (ambientLevel (0.079), 1.0f));
    CHECK (juce::exactlyEqual (ambientLevel (0.081), 0.0f));
}

// ---- The transient detector (T-0.7-03)
// ---------------------------------------------------------------------------------

TEST_CASE ("A transient out of silence strikes once; a held note doesn't strike again", "[unit][lightning]")
{
    StrikeDetector d;
    CHECK (feed (d, 0.0f, 1.0) == 0);
    CHECK (d.addFrame (0.3f, 0.3f, kFrame)); // the attack
    CHECK_THAT (d.getStrikePeak(), WithinAbs (0.3, 1.0e-6));
    CHECK (feed (d, 0.3f, 5.0) == 0); // held
}

TEST_CASE ("Nothing quieter than the floor strikes", "[unit][lightning]")
{
    StrikeDetector d;
    feed (d, 0.0f, 1.0);
    const auto quiet = juce::Decibels::decibelsToGain (tokens::floorDb - 5.0f);
    CHECK (feed (d, quiet, 0.5) == 0);
}

TEST_CASE ("Strikes never come faster than 3 a second; extra transients are dropped", "[unit][lightning]")
{
    // A hit every 100 ms (10 a second).
    StrikeDetector d;
    std::vector<double> times;
    double t = 0.0;
    for (int hit = 0; hit < 50; ++hit)
    {
        for (double s = 0.0; s < 0.1; s += kFrame, t += kFrame)
            if (d.addFrame (s < 0.01 ? 0.5f : 0.02f, 0.5f, kFrame))
                times.push_back (t);
    }
    REQUIRE (times.size() >= 10);
    for (std::size_t i = 1; i < times.size(); ++i)
        CHECK (times[i] - times[i - 1] >= tokens::minStrikeInterval - kFrame);
    CHECK (times.size() <= static_cast<std::size_t> (std::ceil (t * 3.0)) + 1);
}

TEST_CASE ("The threshold adapts: punchy music needs big hits, soft music strikes on soft rises", "[unit][lightning]")
{
    // Soft: notes rising ~6 dB over a sustained level, every half second
    // (at full sensitivity, as with Static at 100%).
    StrikeDetector soft;
    soft.setSensitivity (1.0f);
    feed (soft, 0.1f, 1.0);
    auto softStrikes = 0;
    for (int note = 0; note < 8; ++note)
    {
        softStrikes += feed (soft, 0.2f, 0.05);
        softStrikes += feed (soft, 0.1f, 0.45);
    }
    CHECK (soft.getThresholdDb() < 6.0f);
    CHECK (softStrikes >= 6);

    // Punchy: 20 dB hits teach it a high threshold; then 6 dB rises don't strike.
    StrikeDetector punchy;
    feed (punchy, 0.05f, 1.0);
    for (int hit = 0; hit < 4; ++hit)
    {
        feed (punchy, 0.5f, 0.02);
        feed (punchy, 0.05f, 0.48);
    }
    CHECK (punchy.getThresholdDb() > 9.0f);
    auto smallStrikes = 0;
    for (int note = 0; note < 3; ++note)
    {
        smallStrikes += feed (punchy, 0.1f, 0.05);
        smallStrikes += feed (punchy, 0.05f, 0.45);
    }
    CHECK (smallStrikes == 0);
    CHECK (punchy.getThresholdDb() <= tokens::onsetMaxDb);
}

TEST_CASE ("Bolt brightness follows the output level; silence fades it to nothing", "[unit][lightning]")
{
    StrikeDetector d;
    feed (d, 0.5f, 0.5, 0.5f); // −6 dBFS out
    CHECK (d.getBrightness() > 0.95f);

    feed (d, 0.02f, 1.0, 0.02f); // −34 dBFS
    const auto quieter = d.getBrightness();
    CHECK (quieter < 0.8f);
    CHECK (quieter > tokens::dimBrightness);

    // 10 ms attack: a loud frame brings it most of the way back quickly.
    feed (d, 0.5f, 0.02, 0.5f);
    CHECK (d.getBrightness() > quieter + 0.1f);

    feed (d, 0.0f, tokens::silenceSeconds + 0.5, 0.0f);
    CHECK (juce::exactlyEqual (d.getBrightness(), 0.0f));
}

// ---- The view (T-0.7-02)
// -----------------------------------------------------------------------------------------------------

TEST_CASE ("The view keeps at most three bolts, inside the well and above the toggle", "[unit][lightning][ui]")
{
    ui::LightningView view;
    view.setBounds (0, 0, 1000, 230);
    view.setKeepClear ({880, 190, 110, 28});

    bool self = true, children = true;
    view.getInterceptsMouseClicks (self, children);
    CHECK_FALSE (self);

    view.strike (0.5f, 0.0f); // nothing to show at zero brightness
    CHECK (view.getNumBolts() == 0);

    // Four at once: the flash limit lets at most three through (fewer when one
    // books its re-flicker), and never more than maxBolts show.
    for (int i = 0; i < 4; ++i)
        view.strike (0.5f, 1.0f);
    CHECK (view.getNumBolts() >= 2);
    CHECK (view.getNumBolts() <= tokens::maxBolts);
    CHECK (view.getDroppedCount() >= 1);
    CHECK (view.getAmbient() > 0.0f);

    for (int b = 0; b < view.getNumBolts(); ++b)
        for (const auto& s : view.getBolt (b).strands)
            for (const auto& p : s.points)
            {
                CHECK (view.getLocalBounds().toFloat().contains (p));
                CHECK (p.y <= 190.0f);
            }

    // The ambient flash ends after 80 ms, the bolts after their timeline.
    CHECK (view.advance (0.1));
    CHECK (juce::exactlyEqual (view.getAmbient(), 0.0f));
    view.advance (0.3);
    CHECK (view.getNumBolts() == 0);
    CHECK_FALSE (view.advance (0.1)); // nothing left to draw
}

TEST_CASE ("A bolt draws its glow, body and core", "[unit][lightning][ui]")
{
    ui::LightningView view;
    view.setBounds (0, 0, 400, 230);
    view.strike (0.5f, 1.0f);
    view.advance (0.1); // past the ambient flash, still bright (or flickering at 40%)

    juce::Image image (juce::Image::ARGB, 400, 230, true, juce::SoftwareImageType());
    {
        juce::Graphics g (image);
        view.paint (g);
    }

    // Some pixel on the main strand is lit; a corner far from it isn't.
    const auto& main = view.getBolt (0).strands.front();
    auto lit = 0;
    for (const auto& p : main.points)
        if (image.getPixelAt (juce::roundToInt (p.x), juce::roundToInt (p.y)).getAlpha() > 0)
            ++lit;
    CHECK (lit > static_cast<int> (main.points.size()) / 2);
    CHECK (image.getPixelAt (2, 228).getAlpha() == 0);
}

// ---- In the editor (T-0.7-03)
// ------------------------------------------------------------------------------------------------

TEST_CASE ("Lightning strikes on the layer's transients while the melody plays", "[unit][lightning][ui][plugin]")
{
    const auto settingsFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getChildFile ("ThunderLightningTests-" + juce::Uuid().toString() + ".xml");
    test::chooseMotion (settingsFile);
    ThunderProcessor processor;
    processor.setSettingsFile (settingsFile);
    processor.prepareToPlay (48000.0, 512);
    test::TestPlayHead head;
    processor.setPlayHead (&head);
    std::unique_ptr<juce::AudioProcessorEditor> owner (processor.createEditorAndMakeActive());
    auto& e = dynamic_cast<ThunderEditor&> (*owner);
    auto& lightning = e.getVisualiser().getLightning();

    // Static at full Amount: the most sensitive (UI.md §12.3).
    for (const auto& [id, value] :
         {std::pair<params::ParamId, float>{params::ParamId::staticOn, 1.0f}, {params::ParamId::staticAmount, 100.0f}})
    {
        auto& p = ui::parameter (processor.getValueTreeState(), id);
        p.setValueNotifyingHost (p.convertTo0to1 (value));
    }

    const auto frame = [&e] { e.updateVisuals (true, 512.0 / 48000.0); };
    e.updateVisuals (true, 0.0);
    test::playMelody (processor, head, 6.0, 48000.0, 512, frame);
    const auto struck = lightning.getStrikeCount();
    CHECK (struck >= 3);  // the melody's notes strike (12 notes in 6 s)
    CHECK (struck <= 18); // never more than 3 a second

    // A frozen display doesn't strike.
    e.getVisualModel().setFrozen (true);
    test::playMelody (processor, head, 3.0, 48000.0, 512, frame);
    CHECK (lightning.getStrikeCount() == struck);

    processor.setPlayHead (nullptr);
    processor.editorBeingDeleted (owner.get());
    settingsFile.deleteFile();
}

TEST_CASE ("A bolt's bounds contain every point", "[unit][lightning]")
{
    const auto bolt = generateBolt ({100.0f, 0.0f}, {160.0f, 150.0f}, 3);
    const auto bounds = bolt.getBounds().expanded (1.0e-3f);
    CHECK (bounds.getHeight() >= 150.0f);
    for (const auto& s : bolt.strands)
        for (const auto& p : s.points)
            CHECK (bounds.contains (p));
}
