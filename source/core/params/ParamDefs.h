#pragma once

#include "Enums.h"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

// The single source of truth for Thunder's host parameters (PRD §9).
//
// Every parameter's ID, display name, range, default, unit and the release that
// introduced it lives in kParamDefs below, and nowhere else. The APVTS layout,
// the parameter snapshot read by the audio thread, value formatting and (later)
// presets are all built from this table.
//
// Rules (PRD §9, ARCHITECTURE §10):
// - Parameter IDs are permanent: never rename or remove one.
// - New parameters are appended to their group with versionHint set to the
//   release that ships them; their default must reproduce the old sound.
// - Choice lists only ever grow at the end (see Enums.h).

namespace thunder::params
{

enum class Kind
{
    Float,
    Int,
    Bool,
    Choice
};

// How a Float maps onto a host's 0..1 control range.
enum class Taper
{
    Linear,
    Log // equal ratios get equal travel: frequencies, times, rates
};

enum class Unit
{
    None,
    Percent,
    Decibels,
    Hertz,
    Milliseconds,
    Semitones,
    Cents,
    GrainsPerSecond
};

// Parameter groups, in the order hosts and the generic editor show them.
enum class Group
{
    Output,
    Key,
    Pitch,
    Time,
    Grains,
    Static,
    Space,
    Tone,
    Width,
    Shape,
    Duck,
    Perform
};

struct GroupDef
{
    Group group = Group::Output;
    std::string_view id;
    std::string_view name;
};

inline constexpr std::array<GroupDef, 12> kGroupDefs{{
    {Group::Output, "output", "Output"},
    {Group::Key, "key", "Key"},
    {Group::Pitch, "pitch", "Pitch"},
    {Group::Time, "time", "Time"},
    {Group::Grains, "grains", "Grains"},
    {Group::Static, "static", "Static"},
    {Group::Space, "space", "Space"},
    {Group::Tone, "tone", "Tone"},
    {Group::Width, "width", "Width"},
    {Group::Shape, "shape", "Shape"},
    {Group::Duck, "duck", "Duck"},
    {Group::Perform, "perform", "Perform"},
}};

// JUCE parameter version hint for a Thunder release: v0.5 → 5, v1.0 → 100,
// v2.0 → 200. Hosts (AU in particular) use it to tell parameters added in a
// later release apart from the original set, so it must only ever increase.
constexpr int versionHint (int major, int minor) noexcept
{
    return major * 100 + minor;
}

inline constexpr int kReleaseV0_5 = versionHint (0, 5);
inline constexpr int kReleaseV1_0 = versionHint (1, 0);

// Index of every parameter. The order matches kParamDefs (checked at compile
// time below) and is the order hosts list the parameters in.
enum class ParamId : int
{
    // Global / Output (PRD §9.1)
    outputMode,
    mix,
    inputGain,
    layerLevel,
    outputGain,
    autoGain,
    safetyLimiter,
    // Key (§9.2)
    keyRoot,
    keyScale,
    // Pitch (§9.3)
    pitchOn,
    pitchMode,
    pitchOctave,
    pitchInterval,
    pitchDirection,
    pitchSemitones,
    pitchFine,
    pitchFormant,
    pitchMix,
    // Time (§9.4)
    timeOn,
    halftimeSpeed,
    halftimeSmooth,
    // Grains (§9.5)
    grainOn,
    grainAmount,
    grainSize,
    grainDensity,
    grainPosSpread,
    grainPitchSpread,
    grainPanSpread,
    grainReverse,
    grainScaleLock,
    grainSync,
    grainFreeze,
    // Static (§9.6)
    staticOn,
    staticAmount,
    staticRate,
    staticSync,
    staticRateSynced,
    staticCharacter,
    staticTgtPosition,
    staticTgtPitch,
    staticTgtFilter,
    staticTgtPan,
    staticTgtLevel,
    // Space (§9.7)
    spaceOn,
    spaceMode,
    spaceMix,
    spaceDecay,
    spacePreDelay,
    spaceDamping,
    // Tone (§9.8)
    toneOn,
    hpFreq,
    hpSlope,
    lpFreq,
    lpSlope,
    // Width (§9.9)
    widthOn,
    widthAmount,
    autopanOn,
    autopanRate,
    autopanDepth,
    // Shape (§9.10)
    shapeOn,
    shapeType,
    shapeRate,
    shapeDepth,
    shapeSmooth,
    // Duck (§9.11)
    duckOn,
    duckAmount,
    duckRelease,
    duckMode,
    // Performance (§9.14)
    morphAmount,
    stormAmount,
    strikeClap,
    strikeScatter,
    riserBars,
    riserGo,
    variationAmount,
};

inline constexpr std::size_t kNumParams = static_cast<std::size_t> (ParamId::variationAmount) + 1;

constexpr std::size_t toIndex (ParamId id) noexcept
{
    return static_cast<std::size_t> (id);
}

struct ParamDef
{
    ParamId param = ParamId::outputMode;
    std::string_view id;   // PRD §9 ID: permanent
    std::string_view name; // shown by hosts
    Group group = Group::Output;
    Kind kind = Kind::Float;
    float min = 0.0f; // real units; for Bool 0/1, for Choice 0..(choices - 1)
    float max = 1.0f;
    float def = 0.0f;
    Taper taper = Taper::Linear;
    Unit unit = Unit::None;
    std::span<const std::string_view> choices;
    int versionHint = kReleaseV0_5;
    bool automatable = true;

    constexpr int numChoices() const noexcept { return static_cast<int> (choices.size()); }
};

namespace detail
{
constexpr ParamDef make (ParamId param, std::string_view id, std::string_view name, Group group, Kind kind, float min,
                         float max, float def, Taper taper, Unit unit,
                         std::span<const std::string_view> choices = {}) noexcept
{
    ParamDef d;
    d.param = param;
    d.id = id;
    d.name = name;
    d.group = group;
    d.kind = kind;
    d.min = min;
    d.max = max;
    d.def = def;
    d.taper = taper;
    d.unit = unit;
    d.choices = choices;
    return d;
}

constexpr ParamDef floatParam (ParamId p, std::string_view id, std::string_view name, Group g, float min, float max,
                               float def, Unit unit, Taper taper = Taper::Linear) noexcept
{
    return make (p, id, name, g, Kind::Float, min, max, def, taper, unit);
}

constexpr ParamDef intParam (ParamId p, std::string_view id, std::string_view name, Group g, int min, int max, int def,
                             Unit unit) noexcept
{
    return make (p, id, name, g, Kind::Int, static_cast<float> (min), static_cast<float> (max),
                 static_cast<float> (def), Taper::Linear, unit);
}

constexpr ParamDef boolParam (ParamId p, std::string_view id, std::string_view name, Group g, bool def) noexcept
{
    return make (p, id, name, g, Kind::Bool, 0.0f, 1.0f, def ? 1.0f : 0.0f, Taper::Linear, Unit::None);
}

template<typename Enum, std::size_t N>
constexpr ParamDef choiceParam (ParamId p, std::string_view id, std::string_view name, Group g,
                                const std::array<std::string_view, N>& choices, Enum def) noexcept
{
    return make (p, id, name, g, Kind::Choice, 0.0f, static_cast<float> (N - 1), static_cast<float> (enumIndex (def)),
                 Taper::Linear, Unit::None, std::span (choices));
}

// A choice whose default is given by index (for lists without their own enum).
template<std::size_t N>
constexpr ParamDef choiceParamAt (ParamId p, std::string_view id, std::string_view name, Group g,
                                  const std::array<std::string_view, N>& choices, std::size_t def) noexcept
{
    return make (p, id, name, g, Kind::Choice, 0.0f, static_cast<float> (N - 1), static_cast<float> (def),
                 Taper::Linear, Unit::None, std::span (choices));
}

// A parameter that first ships in v1.0 (PRD §9 rules: the version hint is the
// release that introduced it).
constexpr ParamDef v1_0 (ParamDef d) noexcept
{
    d.versionHint = kReleaseV1_0;
    return d;
}
} // namespace detail

// clang-format off
inline constexpr std::array<ParamDef, kNumParams> kParamDefs {{
    // ---- Global / Output (PRD §9.1) -------------------------------------------------------------------------------
    detail::choiceParam (ParamId::outputMode,   "outputMode",   "Output Mode",    Group::Output, kOutputModeNames, OutputMode::Blend),
    detail::floatParam  (ParamId::mix,          "mix",          "Mix",            Group::Output, 0.0f, 100.0f, 50.0f, Unit::Percent),
    detail::floatParam  (ParamId::inputGain,    "inputGain",    "Input Gain",     Group::Output, -24.0f, 24.0f, 0.0f, Unit::Decibels),
    detail::floatParam  (ParamId::layerLevel,   "layerLevel",   "Layer Level",    Group::Output, -36.0f, 12.0f, 0.0f, Unit::Decibels),
    detail::floatParam  (ParamId::outputGain,   "outputGain",   "Output Gain",    Group::Output, -24.0f, 24.0f, 0.0f, Unit::Decibels),
    detail::boolParam   (ParamId::autoGain,     "autoGain",     "Auto Gain",      Group::Output, true),
    detail::boolParam   (ParamId::safetyLimiter,"safetyLimiter","Safety Limiter", Group::Output, true),

    // ---- Key (§9.2) ------------------------------------------------------------------------------------------------
    detail::choiceParam (ParamId::keyRoot,      "keyRoot",      "Key Root",       Group::Key, kKeyRootNames, KeyRoot::C),
    detail::choiceParam (ParamId::keyScale,     "keyScale",     "Scale",          Group::Key, kScaleNames, Scale::NaturalMinor),

    // ---- Pitch (§9.3) ----------------------------------------------------------------------------------------------
    detail::boolParam   (ParamId::pitchOn,       "pitchOn",       "Pitch On",          Group::Pitch, true),
    detail::choiceParam (ParamId::pitchMode,     "pitchMode",     "Pitch Mode",        Group::Pitch, kPitchModeNames, PitchMode::Octave),
    detail::choiceParam (ParamId::pitchOctave,   "pitchOctave",   "Octave",            Group::Pitch, kPitchOctaveNames, PitchOctave::Up1),
    detail::choiceParam (ParamId::pitchInterval, "pitchInterval", "Interval",          Group::Pitch, kPitchIntervalNames, PitchInterval::Third),
    detail::choiceParam (ParamId::pitchDirection,"pitchDirection","Direction",         Group::Pitch, kPitchDirectionNames, PitchDirection::Up),
    detail::intParam    (ParamId::pitchSemitones,"pitchSemitones","Semitones",         Group::Pitch, -24, 24, 0, Unit::Semitones),
    detail::floatParam  (ParamId::pitchFine,     "pitchFine",     "Fine",              Group::Pitch, -100.0f, 100.0f, 0.0f, Unit::Cents),
    detail::boolParam   (ParamId::pitchFormant,  "pitchFormant",  "Preserve Formants", Group::Pitch, true),
    detail::floatParam  (ParamId::pitchMix,      "pitchMix",      "Pitch Mix",         Group::Pitch, 0.0f, 100.0f, 100.0f, Unit::Percent),

    // ---- Time (§9.4) -----------------------------------------------------------------------------------------------
    detail::v1_0 (detail::boolParam   (ParamId::timeOn,         "timeOn",         "Time On",  Group::Time, false)),
    detail::v1_0 (detail::choiceParam (ParamId::halftimeSpeed,  "halftimeSpeed",  "Halftime", Group::Time, kHalftimeSpeedNames, HalftimeSpeed::Half)),
    detail::v1_0 (detail::floatParam  (ParamId::halftimeSmooth, "halftimeSmooth", "Halftime Smooth", Group::Time, 0.0f, 100.0f, 30.0f, Unit::Percent)),

    // ---- Grains (§9.5) ---------------------------------------------------------------------------------------------
    detail::boolParam   (ParamId::grainOn,          "grainOn",          "Grains On",           Group::Grains, true),
    detail::floatParam  (ParamId::grainAmount,      "grainAmount",      "Grain Amount",        Group::Grains, 0.0f, 100.0f, 50.0f, Unit::Percent),
    detail::floatParam  (ParamId::grainSize,        "grainSize",        "Grain Size",          Group::Grains, 10.0f, 500.0f, 80.0f, Unit::Milliseconds, Taper::Log),
    detail::floatParam  (ParamId::grainDensity,     "grainDensity",     "Grain Density",       Group::Grains, 1.0f, 100.0f, 20.0f, Unit::GrainsPerSecond, Taper::Log),
    detail::floatParam  (ParamId::grainPosSpread,   "grainPosSpread",   "Position Spread",     Group::Grains, 0.0f, 100.0f, 25.0f, Unit::Percent),
    detail::floatParam  (ParamId::grainPitchSpread, "grainPitchSpread", "Pitch Spread",        Group::Grains, 0.0f, 12.0f, 0.0f, Unit::Semitones),
    detail::floatParam  (ParamId::grainPanSpread,   "grainPanSpread",   "Pan Spread",          Group::Grains, 0.0f, 100.0f, 40.0f, Unit::Percent),
    detail::floatParam  (ParamId::grainReverse,     "grainReverse",     "Reverse Probability", Group::Grains, 0.0f, 100.0f, 0.0f, Unit::Percent),
    detail::v1_0 (detail::boolParam (ParamId::grainScaleLock, "grainScaleLock", "Scale Lock",       Group::Grains, true)),
    detail::v1_0 (detail::boolParam (ParamId::grainSync,      "grainSync",      "Grain Tempo Sync", Group::Grains, false)),
    detail::v1_0 (detail::boolParam (ParamId::grainFreeze,    "grainFreeze",    "Freeze",           Group::Grains, false)),

    // ---- Static (§9.6) ---------------------------------------------------------------------------------------------
    // Target defaults (DEC-050): position, filter and pan move; pitch and level
    // stay put until asked for.
    detail::v1_0 (detail::boolParam     (ParamId::staticOn,          "staticOn",          "Static On",          Group::Static, false)),
    detail::v1_0 (detail::floatParam    (ParamId::staticAmount,      "staticAmount",      "Static Amount",      Group::Static, 0.0f, 100.0f, 30.0f, Unit::Percent)),
    detail::v1_0 (detail::floatParam    (ParamId::staticRate,        "staticRate",        "Static Rate",        Group::Static, 0.05f, 20.0f, 0.5f, Unit::Hertz, Taper::Log)),
    detail::v1_0 (detail::boolParam     (ParamId::staticSync,        "staticSync",        "Static Sync",        Group::Static, false)),
    detail::v1_0 (detail::choiceParam   (ParamId::staticRateSynced,  "staticRateSynced",  "Static Synced Rate", Group::Static, kSyncedRateNames, SyncedRate::Bar1)),
    detail::v1_0 (detail::choiceParam   (ParamId::staticCharacter,   "staticCharacter",   "Static Character",   Group::Static, kStaticCharacterNames, StaticCharacter::Smooth)),
    detail::v1_0 (detail::boolParam     (ParamId::staticTgtPosition, "staticTgtPosition", "Static to Position", Group::Static, true)),
    detail::v1_0 (detail::boolParam     (ParamId::staticTgtPitch,    "staticTgtPitch",    "Static to Pitch",    Group::Static, false)),
    detail::v1_0 (detail::boolParam     (ParamId::staticTgtFilter,   "staticTgtFilter",   "Static to Filter",   Group::Static, true)),
    detail::v1_0 (detail::boolParam     (ParamId::staticTgtPan,      "staticTgtPan",      "Static to Pan",      Group::Static, true)),
    detail::v1_0 (detail::boolParam     (ParamId::staticTgtLevel,    "staticTgtLevel",    "Static to Level",    Group::Static, false)),

    // ---- Space (§9.7) ----------------------------------------------------------------------------------------------
    detail::boolParam   (ParamId::spaceOn,       "spaceOn",       "Space On",    Group::Space, true),
    detail::choiceParam (ParamId::spaceMode,     "spaceMode",     "Space Mode",  Group::Space, kSpaceModeNames, SpaceMode::Cloud),
    detail::floatParam  (ParamId::spaceMix,      "spaceMix",      "Space Mix",   Group::Space, 0.0f, 100.0f, 40.0f, Unit::Percent),
    detail::floatParam  (ParamId::spaceDecay,    "spaceDecay",    "Decay",       Group::Space, 0.0f, 100.0f, 50.0f, Unit::Percent),
    detail::floatParam  (ParamId::spacePreDelay, "spacePreDelay", "Pre-delay",   Group::Space, 0.0f, 250.0f, 20.0f, Unit::Milliseconds),
    detail::floatParam  (ParamId::spaceDamping,  "spaceDamping",  "Damping",     Group::Space, 0.0f, 100.0f, 50.0f, Unit::Percent),

    // ---- Tone (§9.8) -----------------------------------------------------------------------------------------------
    detail::boolParam   (ParamId::toneOn,  "toneOn",  "Tone On",   Group::Tone, true),
    detail::floatParam  (ParamId::hpFreq,  "hpFreq",  "High-pass", Group::Tone, 20.0f, 2000.0f, 200.0f, Unit::Hertz, Taper::Log),
    detail::choiceParam (ParamId::hpSlope, "hpSlope", "HP Slope",  Group::Tone, kFilterSlopeNames, FilterSlope::Db24),
    detail::floatParam  (ParamId::lpFreq,  "lpFreq",  "Low-pass",  Group::Tone, 1000.0f, 20000.0f, 16000.0f, Unit::Hertz, Taper::Log),
    detail::choiceParam (ParamId::lpSlope, "lpSlope", "LP Slope",  Group::Tone, kFilterSlopeNames, FilterSlope::Db12),

    // ---- Width (§9.9) ----------------------------------------------------------------------------------------------
    detail::boolParam   (ParamId::widthOn,     "widthOn",     "Width On", Group::Width, true),
    detail::floatParam  (ParamId::widthAmount, "widthAmount", "Width",    Group::Width, 0.0f, 200.0f, 100.0f, Unit::Percent),
    detail::v1_0 (detail::boolParam   (ParamId::autopanOn,    "autopanOn",    "Autopan",       Group::Width, false)),
    detail::v1_0 (detail::choiceParam (ParamId::autopanRate,  "autopanRate",  "Autopan Rate",  Group::Width, kSyncedRateNames, SyncedRate::Quarter)),
    detail::v1_0 (detail::floatParam  (ParamId::autopanDepth, "autopanDepth", "Autopan Depth", Group::Width, 0.0f, 100.0f, 50.0f, Unit::Percent)),

    // ---- Shape (§9.10) ---------------------------------------------------------------------------------------------
    detail::v1_0 (detail::boolParam     (ParamId::shapeOn,     "shapeOn",     "Shape On",     Group::Shape, false)),
    detail::v1_0 (detail::choiceParam   (ParamId::shapeType,   "shapeType",   "Shape",        Group::Shape, kShapeTypeNames, ShapeType::Pump)),
    detail::v1_0 (detail::choiceParamAt (ParamId::shapeRate,   "shapeRate",   "Shape Rate",   Group::Shape, kShapeRateNames, enumIndex (SyncedRate::Quarter) - kShapeRateOffset)),
    detail::v1_0 (detail::floatParam    (ParamId::shapeDepth,  "shapeDepth",  "Shape Depth",  Group::Shape, 0.0f, 100.0f, 60.0f, Unit::Percent)),
    detail::v1_0 (detail::floatParam    (ParamId::shapeSmooth, "shapeSmooth", "Shape Smooth", Group::Shape, 0.0f, 100.0f, 20.0f, Unit::Percent)),

    // ---- Duck (§9.11) ----------------------------------------------------------------------------------------------
    detail::v1_0 (detail::boolParam   (ParamId::duckOn,      "duckOn",      "Duck On",      Group::Duck, false)),
    detail::v1_0 (detail::floatParam  (ParamId::duckAmount,  "duckAmount",  "Duck Amount",  Group::Duck, 0.0f, 100.0f, 40.0f, Unit::Percent)),
    detail::v1_0 (detail::floatParam  (ParamId::duckRelease, "duckRelease", "Duck Release", Group::Duck, 10.0f, 1000.0f, 150.0f, Unit::Milliseconds, Taper::Log)),
    detail::v1_0 (detail::choiceParam (ParamId::duckMode,    "duckMode",    "Duck Mode",    Group::Duck, kDuckModeNames, DuckMode::Envelope)),

    // ---- Performance (§9.14) ---------------------------------------------------------------------------------------
    detail::v1_0 (detail::floatParam  (ParamId::morphAmount, "morphAmount", "Morph",        Group::Perform, 0.0f, 100.0f, 0.0f, Unit::Percent)),
    detail::v1_0 (detail::floatParam  (ParamId::stormAmount,     "stormAmount",     "Storm",          Group::Perform, 0.0f, 100.0f, 0.0f, Unit::Percent)),
    detail::v1_0 (detail::boolParam   (ParamId::strikeClap,      "strikeClap",      "Thunderclap",    Group::Perform, false)),
    detail::v1_0 (detail::boolParam   (ParamId::strikeScatter,   "strikeScatter",   "Scatter",        Group::Perform, false)),
    detail::v1_0 (detail::choiceParam (ParamId::riserBars,       "riserBars",       "Riser Length",   Group::Perform, kRiserBarsNames, RiserBars::Bars4)),
    detail::v1_0 (detail::boolParam   (ParamId::riserGo,         "riserGo",         "Riser Go",       Group::Perform, false)),
    detail::v1_0 (detail::floatParam  (ParamId::variationAmount, "variationAmount", "Variation",      Group::Perform, 0.0f, 100.0f, 0.0f, Unit::Percent)),
}};
// clang-format on

constexpr const ParamDef& paramDef (ParamId id) noexcept
{
    return kParamDefs[toIndex (id)];
}

constexpr std::optional<ParamId> findParam (std::string_view id) noexcept
{
    for (const auto& d : kParamDefs)
        if (d.id == id)
            return d.param;

    return std::nullopt;
}

constexpr const GroupDef& groupDef (Group group) noexcept
{
    return kGroupDefs[static_cast<std::size_t> (group)];
}

// ---- Compile-time checks on the table -----------------------------------------------------------------------------

namespace detail
{
consteval bool tableIsConsistent()
{
    for (std::size_t i = 0; i < kParamDefs.size(); ++i)
    {
        const auto& d = kParamDefs[i];

        if (toIndex (d.param) != i)
            return false; // table order differs from ParamId order
        if (d.id.empty() || d.name.empty() || !(d.min < d.max) || d.def < d.min || d.def > d.max)
            return false;
        if (d.kind == Kind::Choice && d.numChoices() != static_cast<int> (d.max) + 1)
            return false;
        if (d.taper == Taper::Log && !(d.min > 0.0f))
            return false;
        if (i > 0 && kParamDefs[i - 1].group > d.group)
            return false; // groups must be contiguous and in Group order

        for (std::size_t j = 0; j < i; ++j)
            if (kParamDefs[j].id == d.id)
                return false; // duplicate ID
    }

    for (std::size_t i = 0; i < kGroupDefs.size(); ++i)
        if (static_cast<std::size_t> (kGroupDefs[i].group) != i)
            return false;

    return true;
}
} // namespace detail

static_assert (detail::tableIsConsistent(), "kParamDefs is inconsistent; see detail::tableIsConsistent()");

} // namespace thunder::params
