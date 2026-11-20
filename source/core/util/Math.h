#pragma once

#include <cmath>

namespace thunder::util
{

// [RT] Replaces every NaN and ±Inf in `data` with 0 and returns how many were
// replaced. Bad samples from a host or an upstream plugin must never reach the
// DSP (recursive filters and reverbs would turn them into permanent noise) or
// the speakers (PRD NFR-REL-003).
//
// Relies on IEEE semantics: never build Thunder with -ffast-math or /fp:fast.
inline int replaceNonFinite (float* data, int numSamples) noexcept
{
    int replaced = 0;

    for (int i = 0; i < numSamples; ++i)
    {
        if (!std::isfinite (data[i]))
        {
            data[i] = 0.0f;
            ++replaced;
        }
    }

    return replaced;
}

// [RT] 4-point, third-order Hermite (Catmull-Rom) interpolation between y0 and
// y1, `t` (0 … 1) of the way from y0; ym1 and y2 are the neighbours either side.
// It passes through every sample (t = 0 gives exactly y0) with a continuous
// slope, so a resampled signal has far less of the buzz linear interpolation
// adds (PRD FR-GRAIN-008).
inline float cubicHermite (float ym1, float y0, float y1, float y2, float t) noexcept
{
    const auto c1 = 0.5f * (y1 - ym1);
    const auto c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
    const auto c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
    return ((c3 * t + c2) * t + c1) * t + y0;
}

} // namespace thunder::util
