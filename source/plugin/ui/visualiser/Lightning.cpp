#include "Lightning.h"

#include "core/util/Rng.h"
#include "plugin/ui/theme/ThunderTheme.h"

#include <algorithm>
#include <cmath>

namespace thunder::ui::lightning
{

namespace tokens = theme::lightning;

namespace
{
float toDb (float gain) noexcept
{
    return gain > 1.0e-6f ? 20.0f * std::log10 (gain) : -120.0f;
}

// One-pole step towards `target` for a frame of `seconds`, time constant `tau`.
float follow (float current, float target, double seconds, double tau) noexcept
{
    const auto k = static_cast<float> (1.0 - std::exp (-seconds / tau));
    return current + k * (target - current);
}

// Midpoint displacement of one strand. `branches` receives the branches the
// main strand starts (none for a branch).
std::vector<juce::Point<float>> displace (juce::Point<float> start, juce::Point<float> end, int depth, util::Rng& rng,
                                          std::vector<Strand>* branches, std::uint64_t seed,
                                          juce::Rectangle<float> within)
{
    std::vector<juce::Point<float>> points{start, end};
    const auto length = start.getDistanceFrom (end);
    if (length <= 0.0f || depth <= 0)
        return points;

    auto reach = length * tokens::displacement;
    for (int level = 0; level < depth; ++level)
    {
        std::vector<juce::Point<float>> next;
        next.reserve (points.size() * 2);
        for (std::size_t i = 0; i + 1 < points.size(); ++i)
        {
            const auto a = points[i];
            const auto b = points[i + 1];
            const auto along = b - a;
            const auto segment = std::max (1.0e-3f, a.getDistanceFrom (b));
            const juce::Point<float> normal (-along.y / segment, along.x / segment);

            // A random distance sideways, up to the reach, either way.
            const auto magnitude = reach * rng.nextFloat();
            const auto side = rng.nextFloat() < 0.5f ? -1.0f : 1.0f;
            auto mid = (a + b) * 0.5f + normal * (magnitude * side);
            if (!within.isEmpty())
                mid = within.getConstrainedPoint (mid);

            next.push_back (a);
            next.push_back (mid);

            if (branches != nullptr && rng.nextFloat() < tokens::branchChance)
            {
                // Off the main direction, towards the end, for part of the way left.
                const auto toEnd = end - mid;
                const auto left = mid.getDistanceFrom (end);
                if (left > 1.0f)
                {
                    const auto angle =
                        (tokens::branchMinAngle + (tokens::branchMaxAngle - tokens::branchMinAngle) * rng.nextFloat()) *
                        (rng.nextFloat() < 0.5f ? -1.0f : 1.0f);
                    const auto dir = toEnd / left;
                    const juce::Point<float> turned (dir.x * std::cos (angle) - dir.y * std::sin (angle),
                                                     dir.x * std::sin (angle) + dir.y * std::cos (angle));
                    const auto share =
                        tokens::branchMinLength + (tokens::branchMaxLength - tokens::branchMinLength) * rng.nextFloat();
                    auto branchEnd = mid + turned * (left * share);
                    if (!within.isEmpty())
                        branchEnd = within.getConstrainedPoint (branchEnd);

                    // Each branch its own sequence, so it doesn't shift the main strand's.
                    util::Rng branchRng (seed, 1000u + branches->size());
                    Strand branch;
                    branch.points = displace (mid, branchEnd, std::max (1, depth - tokens::branchDepthDrop), branchRng,
                                              nullptr, seed, within);
                    branch.brightness = tokens::branchBrightness;
                    branches->push_back (std::move (branch));
                }
            }
        }
        next.push_back (points.back());
        points = std::move (next);
        reach *= tokens::jitterDecay;
    }
    return points;
}
} // namespace

juce::Rectangle<float> BoltShape::getBounds() const noexcept
{
    // Min and max: Rectangle::getUnion ignores the empty (zero-size) rectangles
    // single points would make.
    std::vector<juce::Point<float>> all;
    for (const auto& s : strands)
        all.insert (all.end(), s.points.begin(), s.points.end());
    return juce::Rectangle<float>::findAreaContainingPoints (all.data(), static_cast<int> (all.size()));
}

int depthFor (float length) noexcept
{
    return length > tokens::longBolt ? tokens::depthLong : tokens::depthShort;
}

BoltShape generateBolt (juce::Point<float> start, juce::Point<float> end, std::uint64_t seed,
                        juce::Rectangle<float> within)
{
    util::Rng rng (seed);
    BoltShape shape;
    std::vector<Strand> branches;

    Strand main;
    main.points = displace (start, end, depthFor (start.getDistanceFrom (end)), rng, &branches, seed, within);
    main.brightness = 1.0f;

    shape.strands.push_back (std::move (main));
    for (auto& b : branches)
        shape.strands.push_back (std::move (b));
    return shape;
}

double strikeLength (bool flicker) noexcept
{
    return tokens::hold + (flicker ? tokens::flickerGap + tokens::flickerLength : 0.0) + tokens::fadeOut;
}

float strikeLevel (double t, bool flicker) noexcept
{
    if (t < 0.0)
        return 0.0f;
    if (t < tokens::hold)
        return 1.0f;

    auto fadeFrom = 1.0f;
    auto fadeStart = tokens::hold;
    if (flicker)
    {
        if (t < tokens::hold + tokens::flickerGap)
            return 0.0f;
        if (t < tokens::hold + tokens::flickerGap + tokens::flickerLength)
            return tokens::flickerLevel;
        fadeFrom = tokens::flickerLevel;
        fadeStart = tokens::hold + tokens::flickerGap + tokens::flickerLength;
    }

    const auto p = (t - fadeStart) / tokens::fadeOut;
    if (p >= 1.0)
        return 0.0f;
    const auto remaining = static_cast<float> (1.0 - p);
    return fadeFrom * remaining * remaining; // ease-out: fast at first, then gently to nothing
}

float ambientLevel (double t) noexcept
{
    return t >= 0.0 && t < tokens::ambientFlash ? 1.0f : 0.0f;
}

void StrikeDetector::reset() noexcept
{
    *this = {};
}

bool StrikeDetector::addFrame (float layerPeak, float outputPeak, double frameSeconds) noexcept
{
    // The layer peak, held for peakRelease so one noisy frame doesn't count.
    heldPeak =
        std::max (std::abs (layerPeak), heldPeak * static_cast<float> (std::exp (-frameSeconds / tokens::peakRelease)));
    const auto db = toDb (heldPeak);

    // Brightness: the output level, 10 ms up, 250 ms down.
    const auto outDb = toDb (std::abs (outputPeak));
    levelDb = follow (levelDb, outDb, frameSeconds, outDb > levelDb ? tokens::levelAttack : tokens::levelRelease);

    sinceStrike += frameSeconds;
    quietFor = db < tokens::floorDb ? quietFor + frameSeconds : 0.0;

    // The transient test uses the recent level and the typical rise before
    // this frame: the threshold is a share of the rises heard lately.
    const auto rise = db - recentDb;
    lastRise = rise;
    const auto threshold = getThresholdDb();
    if (!armed && rise < tokens::rearmShare * threshold)
        armed = true;

    auto strike = false;
    if (armed && rise > threshold && db > tokens::floorDb && sinceStrike >= tokens::minStrikeInterval)
    {
        strike = true;
        armed = false;
        sinceStrike = 0.0;
        strikePeak = heldPeak;
    }

    // The typical rise: the largest lately, forgotten over riseMemory. Rises
    // out of silence don't count (they would set it far too high).
    typicalRise *= static_cast<float> (std::exp (-frameSeconds / tokens::riseMemory));
    if (recentDb > tokens::floorDb - 6.0f)
        typicalRise = std::max (typicalRise, std::min (rise, 2.0f * tokens::onsetMaxDb));

    // Out of silence the recent level jumps to the new one (the strike above
    // has already used the rise), so the climb doesn't read as more rises.
    if (recentDb < tokens::floorDb && db >= tokens::floorDb)
        recentDb = db;
    else
        recentDb = follow (recentDb, db, frameSeconds, db > recentDb ? tokens::recentAttack : tokens::recentRelease);
    return strike;
}

float StrikeDetector::getThresholdDb() const noexcept
{
    const auto share =
        tokens::onsetShareStrong + (tokens::onsetShareSensitive - tokens::onsetShareStrong) * sensitivity;
    const auto least =
        tokens::onsetMinStrongDb + (tokens::onsetMinSensitiveDb - tokens::onsetMinStrongDb) * sensitivity;
    return std::clamp (share * typicalRise, least, tokens::onsetMaxDb);
}

float StrikeDetector::getBrightness() const noexcept
{
    if (quietFor > tokens::silenceSeconds && levelDb < tokens::floorDb)
        return 0.0f;
    const auto t = std::clamp ((levelDb - tokens::dimLevelDb) / (tokens::fullLevelDb - tokens::dimLevelDb), 0.0f, 1.0f);
    return levelDb < tokens::dimLevelDb - 12.0f ? 0.0f : tokens::dimBrightness + (1.0f - tokens::dimBrightness) * t;
}

int FlashLimiter::countIn (double from) const noexcept
{
    auto n = 0;
    for (const auto t : times)
        if (t >= from && t < from + kWindowSeconds)
            ++n;
    return n;
}

bool FlashLimiter::tryAdd (double time)
{
    // Forget what can no longer share a window with anything new.
    times.erase (
        std::remove_if (times.begin(), times.end(), [time] (double t) { return t < time - 2.0 * kWindowSeconds; }),
        times.end());

    // Every window holding `time` starts in (time - window, time]; the busiest
    // starts at one of the flashes in that range, or at its beginning.
    const auto fits = [this] (double start) { return countIn (start) < kMaxFlashes; };
    if (!fits (time - kWindowSeconds + 1.0e-9))
        return false;
    for (const auto t : times)
        if (t > time - kWindowSeconds && t <= time && !fits (t))
            return false;

    times.push_back (time);
    return true;
}

int FlashLimiter::busiestWindow() const
{
    auto most = 0;
    for (const auto t : times)
        most = std::max (most, countIn (t));
    return most;
}

} // namespace thunder::ui::lightning
