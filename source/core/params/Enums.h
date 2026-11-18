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

} // namespace thunder::params
