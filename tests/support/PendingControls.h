#pragma once

#include "core/params/ParamDefs.h"

#include <algorithm>
#include <array>

// Parameters whose controls arrive with the DSP they drive: Duck, Autopan and
// the grain extras are v1.0 parameters already (the saved-state format changed
// once for all of v1.0), but their modules come later, and the editor shows no
// control that does nothing. Empty this list as each arrives.
namespace thunder::test
{

inline constexpr std::array<params::ParamId, 10> kControlsArrivingLater{
    params::ParamId::grainScaleLock, params::ParamId::grainSync,   params::ParamId::grainFreeze,
    params::ParamId::autopanOn,      params::ParamId::autopanRate, params::ParamId::autopanDepth,
    params::ParamId::duckOn,         params::ParamId::duckAmount,  params::ParamId::duckRelease,
    params::ParamId::duckMode};

inline bool controlArrivesLater (params::ParamId id)
{
    return std::find (kControlsArrivingLater.begin(), kControlsArrivingLater.end(), id) != kControlsArrivingLater.end();
}

} // namespace thunder::test
