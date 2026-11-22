#pragma once

#include "core/engine/Module.h"
#include "core/host/MusicalClock.h"
#include "core/util/Smoother.h"

#include <array>
#include <vector>

namespace thunder::dsp
{

// Tempo-synced volume shapes (PRD §7.9, FR-SHAPE-001…004; PRD DEC-050).
//
// One cycle of the chosen shape lasts the chosen note value (2 bars … 1/16
// triplet). The gain is a function of the song position, so the shape stays
// in phase after stops, loops and jumps, and a bounce matches playback:
// - Note values restart on every bar line, so a dotted or triplet rate that
//   doesn't divide the bar still starts each bar on its downbeat.
// - Bar values count bars from the song start (1 bar, 2 bars).
// While the host is stopped the shape keeps running at its tempo
// (MusicalClock).
//
// Shapes over one cycle, φ = 0 … 1 (1 = full level):
//   Pump       1 − (1 − φ)³: ducks on the beat and swells back, like sidechain
//              compression
//   Swell      sin²(πφ/2): rises from silence to full over the cycle
//   Gate       full for the first half, silent for the second
//   Ramp Up    φ;  Ramp Down  1 − φ
//   Chop       four short slices, each full for its first 60%
// Depth sets how far the gain dips: gain = 1 − depth·(1 − shape).
//
// Smooth softens the edges by averaging the shape over a window centred on
// each point (a box filter, from a running integral of the shape, so it costs
// the same at any width): 0% = 1.5 ms, just enough to avoid clicks, 100% = half
// a cycle. Centred means the smoothing never shifts the shape off the grid.
// A 1 ms one-pole on the final gain catches the jumps at transport starts,
// loops and rate changes.
class ShapeModule final : public Module
{
public:
    static constexpr int kTableSize = 4096;
    static constexpr int kNumShapes = 6;

    void prepare (const PrepareSpec& spec) override;
    void reset() noexcept override;
    void process (juce::dsp::AudioBlock<float>& block, const ProcessContext& context) noexcept override;

    // The shape, 0 … 1, at phase `phase` (0 … 1) with smoothing window `width`
    // (a share of the cycle, > 0). For tests and the shape display.
    static float shapeAt (params::ShapeType shape, double phase) noexcept;
    float smoothedAt (params::ShapeType shape, double phase, double width) const noexcept;

    // For tests: the gain applied to the last sample processed.
    float getLastGain() const noexcept { return gain; }

private:
    double integralAt (int shape, double x) const noexcept;

    double sampleRate = 44100.0;
    int numChannels = 2;
    MusicalClock clock;
    util::LinearSmoother depth;
    util::LinearSmoother smoothing;
    bool firstChunk = true;

    // Running integral of each shape over one cycle, kTableSize + 1 points.
    std::array<std::vector<double>, kNumShapes> integrals;

    float gain = 1.0f;      // after the one-pole
    float gainCoeff = 0.0f; // one-pole coefficient per sample
};

} // namespace thunder::dsp
