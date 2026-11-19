#pragma once

#include "Scale.h"

#include "core/params/ParamSnapshot.h"

namespace thunder::music
{

// The key set by the Key Root and Scale parameters (PRD FR-KEY-001; set by
// hand until key detection arrives in v1.0).
inline Key keyFromParams (const params::ParamSnapshot& snapshot) noexcept
{
    using params::ParamId;
    return {static_cast<int> (params::enumIndex (snapshot.getChoice<params::KeyRoot> (ParamId::keyRoot))),
            snapshot.getChoice<Scale> (ParamId::keyScale)};
}

} // namespace thunder::music
