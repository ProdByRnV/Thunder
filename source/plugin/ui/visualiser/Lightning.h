#pragma once

#include <juce_graphics/juce_graphics.h>

#include <cstdint>
#include <vector>

// Lightning in the visualiser well (UI.md §12; PRD UI-FX-001, UI-FX-002): the
// parts that don't draw, so they can be tested without rendering.
//
// - generateBolt: a bolt's shape from a start point to an end point, by
//   midpoint displacement with random branches. The same seed always gives the
//   same bolt.
// - strikeLevel / ambientLevel: how bright a strike and the well's flash are,
//   a given time after the strike.
// - StrikeDetector: reads the visualiser's frames, finds the layer's
//   transients (when to strike) and follows the output level (how bright).
// Message thread only.

namespace thunder::ui::lightning
{

// One line of a bolt: the main strand or a branch.
struct Strand
{
    std::vector<juce::Point<float>> points;
    float brightness = 1.0f; // 1 for the main strand, less for branches
};

struct BoltShape
{
    std::vector<Strand> strands; // the main strand first

    juce::Rectangle<float> getBounds() const noexcept;
};

// UI.md §12.2: the main strand from `start` to `end` (exactly), subdivided
// depthLong times (depthShort for a short bolt). Each level moves every new
// midpoint sideways by up to the segment length × displacement, the reach
// shrinking by jitterDecay a level (a random share of it, either side). At each new midpoint of the main strand a
// branch may start (branchChance): it heads off at 20–46° from the main
// direction for 30–60% of the distance left to the end, with two fewer
// levels and branchBrightness. Branches don't branch.
// With `within` (not empty), every point is kept inside it.
BoltShape generateBolt (juce::Point<float> start, juce::Point<float> end, std::uint64_t seed,
                        juce::Rectangle<float> within = {});

// How many subdivision levels a bolt of this length gets.
int depthFor (float length) noexcept;

// UI.md §12.4, `t` seconds after a strike (0 … 1): full for `hold`, then
// either a fade-out over `fadeOut` (ease-out), or with `flicker` a dark gap,
// one re-flicker at flickerLevel and the fade-out from there. 0 once over.
float strikeLevel (double t, bool flicker) noexcept;
double strikeLength (bool flicker) noexcept;

// The well's ambient flash, 0 … 1 (× ambientLevel of brightening).
float ambientLevel (double t) noexcept;

// UI.md §12.3: finds the layer's transients in the visualiser frames and
// follows the output level.
// - The layer's peak is held for 20 ms (one noisy frame doesn't count) and
//   compared with its recent level (a follower with 120 ms attack and 300 ms
//   release).
// - A transient rises above the recent level by the threshold, which adapts:
//   60% of the largest rise heard in the last few seconds, kept between 4 and
//   12 dB. A layer with soft attacks strikes on its softer rises; a punchy one
//   only on its bigger hits. The peak must be above floorDb.
// - After a strike the rise must fall below half the threshold before the
//   next, so a held note strikes once.
// - At most one strike per minStrikeInterval (3 a second, UI.md §12.5);
//   transients in between are dropped, not queued.
// - After silenceSeconds below floorDb, nothing strikes and the brightness
//   falls to zero with the level.
// - Brightness follows the output's peak level (10 ms attack, 250 ms
//   release): dimBrightness at dimLevelDb, full from fullLevelDb, zero in
//   silence.
class StrikeDetector
{
public:
    void reset() noexcept;

    // One frame: the layer's and the output's largest |sample| and how long
    // the frame lasts. True if a bolt should strike at this frame.
    bool addFrame (float layerPeak, float outputPeak, double frameSeconds) noexcept;

    float getBrightness() const noexcept;                       // 0 … 1
    float getStrikePeak() const noexcept { return strikePeak; } // the layer peak of the last strike
    float getRecentDb() const noexcept { return recentDb; }
    float getLastRiseDb() const noexcept { return lastRise; } // the last frame's rise above the recent level
    float getThresholdDb() const noexcept;                    // how far the next transient must rise

private:
    float heldPeak = 0.0f;
    float typicalRise = 0.0f;
    float recentDb = -120.0f;
    float levelDb = -120.0f;
    float strikePeak = 0.0f;
    float lastRise = 0.0f;
    double sinceStrike = 1.0e9;
    double quietFor = 1.0e9;
    bool armed = true;
};

} // namespace thunder::ui::lightning
