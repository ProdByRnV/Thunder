#pragma once

#include "core/host/TransportState.h"
#include "core/params/ParamSnapshot.h"

#include <juce_dsp/juce_dsp.h>

// The contract every DSP module in the layer chain follows (PRD NFR-MAINT-001,
// ARCHITECTURE §5.1): prepare once, process chunks, reset on demand, and report
// a constant latency.

namespace thunder
{

struct PrepareSpec
{
    double sampleRate = 44100.0;
    int maxBlockSize = 256; // the engine's chunk size, never the host's block size
    int numChannels = 2;
};

// What a module gets with every chunk. Modules read only their own parameters
// from the snapshot and smooth them themselves.
struct ProcessContext
{
    const TransportState& transport;
    const params::ParamSnapshot& params;
};

class Module
{
public:
    virtual ~Module() = default;

    // [NRT] May allocate. Called before processing and on every sample-rate change.
    virtual void prepare (const PrepareSpec& spec) = 0;

    // [RT] Clears all audio state (tails, buffers). Parameter smoothing jumps to
    // the current targets.
    virtual void reset() noexcept = 0;

    // [RT] Processes one chunk (≤ PrepareSpec::maxBlockSize samples) in place.
    virtual void process (juce::dsp::AudioBlock<float>& block, const ProcessContext& context) noexcept = 0;

    // Constant after prepare(): it must not depend on parameters (ARCHITECTURE §5.3).
    virtual int getLatencySamples() const noexcept { return 0; }

    virtual double getTailSeconds() const noexcept { return 0.0; }
};

} // namespace thunder
