#include "IntervalFollower.h"

#include <cmath>

namespace thunder::dsp
{

void IntervalFollower::prepare (double sampleRate) noexcept
{
    holdSamples = static_cast<int> (std::lround (sampleRate * kFallbackHoldSeconds));
    cacheValid = false;
    reset();
}

void IntervalFollower::reset() noexcept
{
    tracking = false;
    note = -1;
    unvoicedSamples = 0;
}

int IntervalFollower::update (const music::PitchEstimate& estimate, const music::Key& key,
                              params::PitchInterval interval, params::PitchDirection direction, int numSamples) noexcept
{
    if (estimate.voiced && estimate.note >= 0)
    {
        tracking = true;
        note = estimate.note;
        unvoicedSamples = 0;
    }
    else if (tracking)
    {
        unvoicedSamples += numSamples;
        if (unvoicedSamples >= holdSamples)
            reset();
    }

    if (tracking)
        return music::IntervalMapper::shiftFor (key, note, interval, direction);

    return fixedShift (key, interval, direction);
}

int IntervalFollower::fixedShift (const music::Key& key, params::PitchInterval interval,
                                  params::PitchDirection direction) noexcept
{
    if (!cacheValid || !(key == cachedKey) || interval != cachedInterval || direction != cachedDirection)
    {
        cachedKey = key;
        cachedInterval = interval;
        cachedDirection = direction;
        cachedFixedShift = music::IntervalMapper::bestFixedShift (key, interval, direction);
        cacheValid = true;
    }

    return cachedFixedShift;
}

} // namespace thunder::dsp
