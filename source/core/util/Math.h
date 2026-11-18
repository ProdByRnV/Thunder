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

} // namespace thunder::util
