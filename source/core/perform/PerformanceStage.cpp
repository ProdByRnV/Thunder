#include "PerformanceStage.h"

#include <algorithm>
#include <cmath>

namespace thunder::perform
{

using params::Kind;
using params::ParamId;

float morphValue (const params::ParamDef& def, float a, float b, float t) noexcept
{
    if (t <= 0.0f)
        return a;
    if (t >= 1.0f)
        return b;

    switch (def.kind)
    {
        case Kind::Bool:
        case Kind::Choice:
            return t < 0.5f ? a : b;

        case Kind::Int:
            return std::clamp (std::round (a + (b - a) * t), def.min, def.max);

        case Kind::Float:
            break;
    }

    if (def.taper == params::Taper::Log && a > 0.0f && b > 0.0f)
        return std::clamp (a * std::pow (b / a, t), def.min, def.max);

    return std::clamp (a + (b - a) * t, def.min, def.max);
}

void applyMorph (const params::ParamSnapshot& a, const MorphSound& b, float t, params::ParamSnapshot& out) noexcept
{
    out = a;
    if (!b.present)
        return;

    for (const auto& def : params::kParamDefs)
    {
        if (!isMorphed (def.param))
            continue;

        const auto i = params::toIndex (def.param);
        out.values[i] = morphValue (def, a.values[i], b.values[i], t);
    }
}

const params::ParamSnapshot& PerformanceStage::apply (const params::ParamSnapshot& host) noexcept
{
    morph.update();
    const auto& b = morph.get();

    const auto t = std::clamp (host.get (ParamId::morphAmount) / 100.0f, 0.0f, 1.0f);
    if (!b.present || t <= 0.0f)
        return host;

    applyMorph (host, b, t, effective);
    return effective;
}

} // namespace thunder::perform
