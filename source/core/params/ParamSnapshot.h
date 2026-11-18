#pragma once

#include "ParamDefs.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace thunder::params
{

// Pointers to the live parameter values, one per parameter in ParamId order.
// The plugin fills them once from the APVTS (getRawParameterValue); they stay
// valid for the processor's lifetime. Values are in real units.
using ParamRefs = std::array<const std::atomic<float>*, kNumParams>;

// A plain copy of every parameter value, taken once per host block.
//
// The audio thread reads the atomics exactly once per block (readFrom), so all
// modules in that block see one consistent set of values; modules then smooth
// towards them themselves (ARCHITECTURE §5.5). No locks, no allocation.
struct ParamSnapshot
{
    std::array<float, kNumParams> values{};

    // A snapshot holding every parameter's default.
    static constexpr ParamSnapshot defaults() noexcept
    {
        ParamSnapshot s;
        for (std::size_t i = 0; i < kNumParams; ++i)
            s.values[i] = kParamDefs[i].def;
        return s;
    }

    // [RT] Copies the current value of every parameter.
    void readFrom (const ParamRefs& refs) noexcept
    {
        for (std::size_t i = 0; i < kNumParams; ++i)
            if (refs[i] != nullptr)
                values[i] = refs[i]->load (std::memory_order_relaxed);
    }

    float get (ParamId id) const noexcept { return values[toIndex (id)]; }

    void set (ParamId id, float value) noexcept { values[toIndex (id)] = value; }

    bool isOn (ParamId id) const noexcept { return get (id) >= 0.5f; }

    int getInt (ParamId id) const noexcept { return static_cast<int> (std::lround (get (id))); }

    // The selected option of a choice parameter, clamped to the valid range.
    template<typename Enum> Enum getChoice (ParamId id) const noexcept
    {
        const auto last = paramDef (id).numChoices() - 1;
        return static_cast<Enum> (std::clamp (getInt (id), 0, std::max (last, 0)));
    }
};

} // namespace thunder::params
