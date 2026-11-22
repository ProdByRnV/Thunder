#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace thunder::dsp
{

// Thunder's own reverb for Space's Plate, Room and Shimmer modes (PRD
// FR-SPACE-001; PRD DEC-051, resolves ARCHITECTURE AQ-03).
//
// A feedback delay network (Jot & Chaigne, "Digital delay networks for
// designing artificial reverberators", AES 1991; Stautner & Puckette 1982):
// - The input is pre-delayed, then smeared by four allpass diffusers per
//   channel (Schroeder 1962).
// - Eight delay lines of different, mutually prime-ish lengths feed back
//   through a Householder matrix (I − 2/N·11ᵀ: lossless and dense), each with
//   its own gain for the decay time (gain = 10^(−3·length / (RT60·rate))) and
//   a one-pole low-pass for the damping.
// - Each line's read point wanders slowly (a sine of a fraction of a
//   millisecond, each line its own rate and phase) so the tail doesn't ring
//   metallically. The phases start the same every time: a render is
//   reproducible.
// - Shimmer: two of the lines pass their feedback through an octave-up pitch
//   shifter (two crossfaded read heads sweeping a short delay at twice speed),
//   so each trip round the loop climbs an octave. The shifter never adds gain,
//   so the loop stays as stable as the plain network.
// - Left and right outputs are signed sums of alternate lines.
//
// Sized once in prepare() for the longest settings at the sample rate;
// process() never allocates. reset() clears the lines (well under a
// millisecond).
class FdnReverb
{
public:
    enum class Character
    {
        plate,
        room,
        shimmer
    };

    static constexpr int kLines = 8;
    static constexpr int kDiffusers = 4;
    static constexpr double kMaxPreDelaySeconds = 0.25;

    // [NRT] Allocates for `sampleRate` and the character's sizes.
    void prepare (double sampleRate, Character character);

    // [RT]
    void reset() noexcept;

    // [RT] Settings, applied from the next sample on (callers smooth them).
    void setDecaySeconds (double seconds) noexcept;
    void setDampingHz (double hertz) noexcept;
    void setPreDelaySeconds (double seconds) noexcept;

    // [RT] Adds the reverb of `inLeft/inRight` to nothing: writes the wet signal
    // only (outLeft/outRight may not alias the inputs).
    void process (const float* inLeft, const float* inRight, float* outLeft, float* outRight, int numSamples) noexcept;

    Character getCharacter() const noexcept { return character; }

private:
    struct Line
    {
        std::vector<float> buffer;
        int mask = 0;
        int write = 0;
        double length = 1.0; // samples, before modulation
        float gain = 0.5f;
        float lowpass = 0.0f; // one-pole state
        // The modulation: a unit phasor (cos, sin) turned by (stepCos, stepSin)
        // each sample, cheaper than a sine per sample.
        double modCos = 1.0, modSin = 0.0;
        double stepCos = 1.0, stepSin = 0.0;
    };

    struct Allpass
    {
        std::vector<float> buffer;
        int length = 1;
        int position = 0;
    };

    struct Shifter
    {
        std::vector<float> buffer;
        int mask = 0;
        int write = 0;
        int position = 0; // samples into the window
    };

    float readLine (const Line& line, double delay) const noexcept;
    std::vector<float> shifterWeights; // sin² over one window
    float shift (Shifter& s, float input) noexcept;
    void updateGains() noexcept;

    double sampleRate = 48000.0;
    Character character = Character::plate;

    std::array<Line, kLines> lines;
    std::array<std::array<Allpass, kDiffusers>, 2> diffusers;
    std::array<Shifter, 2> shifters;
    std::array<std::vector<float>, 2> preDelay;
    int preDelayMask = 0;
    int preDelayWrite = 0;
    int preDelaySamples = 0;

    float diffusion = 0.6f;
    double modDepth = 0.0; // samples
    int shifterWindow = 2048;
    double decaySeconds = 2.0;
    float dampingCoeff = 0.5f;
    float outputGain = 1.0f;
};

} // namespace thunder::dsp
