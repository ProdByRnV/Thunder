#pragma once

#include <array>
#include <cstddef>
#include <string_view>

// Enum types for Thunder's choice parameters, and the names hosts show for them.
//
// The order of each enum MUST match the order of its name list: hosts store a
// choice parameter by index, so neither may ever be reordered once released.
// New options can only be appended. The names are stored in preset files, so
// they are permanent too (ASCII only, so every host displays them correctly).
//
// Each static_assert checks that a name list has exactly one entry per
// enumerator (the last enumerator + 1).

namespace thunder::params
{

template<typename Enum> constexpr std::size_t enumIndex (Enum value) noexcept
{
    return static_cast<std::size_t> (value);
}

enum class OutputMode
{
    Blend,
    LayerOnly
};
inline constexpr std::array<std::string_view, 2> kOutputModeNames{"Blend", "Layer Only"};
static_assert (kOutputModeNames.size() == enumIndex (OutputMode::LayerOnly) + 1);

enum class KeyRoot
{
    C,
    CSharp,
    D,
    DSharp,
    E,
    F,
    FSharp,
    G,
    GSharp,
    A,
    ASharp,
    B
};
inline constexpr std::array<std::string_view, 12> kKeyRootNames{"C",  "C#", "D",  "D#", "E",  "F",
                                                                "F#", "G",  "G#", "A",  "A#", "B"};
static_assert (kKeyRootNames.size() == enumIndex (KeyRoot::B) + 1);

enum class Scale
{
    Major,
    NaturalMinor,
    HarmonicMinor,
    Dorian,
    Phrygian,
    Mixolydian,
    MajorPentatonic,
    MinorPentatonic,
    Chromatic
};
inline constexpr std::array<std::string_view, 9> kScaleNames{
    "Major",      "Natural Minor",    "Harmonic Minor",   "Dorian",   "Phrygian",
    "Mixolydian", "Major Pentatonic", "Minor Pentatonic", "Chromatic"};
static_assert (kScaleNames.size() == enumIndex (Scale::Chromatic) + 1);

enum class PitchMode
{
    Octave,
    Interval,
    Semitone
};
inline constexpr std::array<std::string_view, 3> kPitchModeNames{"Octave", "Interval", "Semitone"};
static_assert (kPitchModeNames.size() == enumIndex (PitchMode::Semitone) + 1);

enum class PitchOctave
{
    Down2,
    Down1,
    Up1,
    Up2
};
inline constexpr std::array<std::string_view, 4> kPitchOctaveNames{"-2", "-1", "+1", "+2"};
static_assert (kPitchOctaveNames.size() == enumIndex (PitchOctave::Up2) + 1);

enum class PitchInterval
{
    Unison,
    Second,
    Third,
    Fourth,
    Fifth,
    Sixth,
    Seventh,
    Octave
};
inline constexpr std::array<std::string_view, 8> kPitchIntervalNames{"Unison", "2nd", "3rd", "4th",
                                                                     "5th",    "6th", "7th", "Octave"};
static_assert (kPitchIntervalNames.size() == enumIndex (PitchInterval::Octave) + 1);

enum class PitchDirection
{
    Up,
    Down
};
inline constexpr std::array<std::string_view, 2> kPitchDirectionNames{"Up", "Down"};
static_assert (kPitchDirectionNames.size() == enumIndex (PitchDirection::Down) + 1);

// All four modes exist from the first release so the choice list (and with it
// the host's automation mapping) never changes. Plate, Room and Shimmer arrive
// in v1.0; until then they use the Cloud engine.
enum class SpaceMode
{
    Cloud,
    Plate,
    Room,
    Shimmer
};
inline constexpr std::array<std::string_view, 4> kSpaceModeNames{"Cloud", "Plate", "Room", "Shimmer"};
static_assert (kSpaceModeNames.size() == enumIndex (SpaceMode::Shimmer) + 1);

enum class FilterSlope
{
    Db12,
    Db24
};
inline constexpr std::array<std::string_view, 2> kFilterSlopeNames{"12 dB/oct", "24 dB/oct"};
static_assert (kFilterSlopeNames.size() == enumIndex (FilterSlope::Db24) + 1);

enum class HalftimeSpeed
{
    Half,
    Quarter
};
inline constexpr std::array<std::string_view, 2> kHalftimeSpeedNames{"1/2", "1/4"};
static_assert (kHalftimeSpeedNames.size() == enumIndex (HalftimeSpeed::Quarter) + 1);

// The one synced rate list every tempo-synced choice uses (PRD §9.13, DEC-021).
// D = dotted (1.5 × as long), T = triplet (2/3 as long).
enum class SyncedRate
{
    Bars4,
    Bars2,
    Bar1,
    Half,
    HalfDotted,
    HalfTriplet,
    Quarter,
    QuarterDotted,
    QuarterTriplet,
    Eighth,
    EighthDotted,
    EighthTriplet,
    Sixteenth,
    SixteenthDotted,
    SixteenthTriplet
};
inline constexpr std::array<std::string_view, 15> kSyncedRateNames{"4 bars", "2 bars", "1 bar", "1/2",   "1/2D",
                                                                   "1/2T",   "1/4",    "1/4D",  "1/4T",  "1/8",
                                                                   "1/8D",   "1/8T",   "1/16",  "1/16D", "1/16T"};
static_assert (kSyncedRateNames.size() == enumIndex (SyncedRate::SixteenthTriplet) + 1);

// Shape's rates are the synced list from "2 bars" on (FR-SHAPE-002): its index
// 0 is SyncedRate::Bars2.
inline constexpr std::size_t kShapeRateOffset = enumIndex (SyncedRate::Bars2);
inline constexpr std::array<std::string_view, 14> kShapeRateNames{
    "2 bars", "1 bar", "1/2", "1/2D", "1/2T", "1/4", "1/4D", "1/4T", "1/8", "1/8D", "1/8T", "1/16", "1/16D", "1/16T"};
static_assert (kShapeRateNames.size() + kShapeRateOffset == kSyncedRateNames.size());

constexpr SyncedRate shapeRateToSynced (std::size_t shapeRateIndex) noexcept
{
    return static_cast<SyncedRate> (shapeRateIndex + kShapeRateOffset);
}

// How long one cycle of a synced rate lasts: in bars for the bar values (so they
// follow the time signature), in quarter notes otherwise.
struct RateLength
{
    double bars = 0.0;
    double quarters = 0.0;
};

constexpr RateLength rateLength (SyncedRate rate) noexcept
{
    constexpr double dotted = 1.5;
    constexpr double triplet = 2.0 / 3.0;

    switch (rate)
    {
        case SyncedRate::Bars4:
            return {4.0, 0.0};
        case SyncedRate::Bars2:
            return {2.0, 0.0};
        case SyncedRate::Bar1:
            return {1.0, 0.0};
        case SyncedRate::Half:
            return {0.0, 2.0};
        case SyncedRate::HalfDotted:
            return {0.0, 2.0 * dotted};
        case SyncedRate::HalfTriplet:
            return {0.0, 2.0 * triplet};
        case SyncedRate::Quarter:
            return {0.0, 1.0};
        case SyncedRate::QuarterDotted:
            return {0.0, dotted};
        case SyncedRate::QuarterTriplet:
            return {0.0, triplet};
        case SyncedRate::Eighth:
            return {0.0, 0.5};
        case SyncedRate::EighthDotted:
            return {0.0, 0.5 * dotted};
        case SyncedRate::EighthTriplet:
            return {0.0, 0.5 * triplet};
        case SyncedRate::Sixteenth:
            return {0.0, 0.25};
        case SyncedRate::SixteenthDotted:
            return {0.0, 0.25 * dotted};
        case SyncedRate::SixteenthTriplet:
            return {0.0, 0.25 * triplet};
    }
    return {0.0, 1.0};
}

enum class StaticCharacter
{
    Smooth,
    Stepped,
    Spark
};
inline constexpr std::array<std::string_view, 3> kStaticCharacterNames{"Smooth", "Stepped", "Spark"};
static_assert (kStaticCharacterNames.size() == enumIndex (StaticCharacter::Spark) + 1);

enum class ShapeType
{
    Pump,
    Swell,
    Gate,
    RampUp,
    RampDown,
    Chop
};
inline constexpr std::array<std::string_view, 6> kShapeTypeNames{"Pump",    "Swell",     "Gate",
                                                                 "Ramp Up", "Ramp Down", "Chop"};
static_assert (kShapeTypeNames.size() == enumIndex (ShapeType::Chop) + 1);

// Spectral ducking arrives in v2.0; listed from the start so the list never
// changes (PRD §9 rules). Until then it ducks like Envelope.
enum class DuckMode
{
    Envelope,
    Spectral
};
inline constexpr std::array<std::string_view, 2> kDuckModeNames{"Envelope", "Spectral"};
static_assert (kDuckModeNames.size() == enumIndex (DuckMode::Spectral) + 1);

} // namespace thunder::params
