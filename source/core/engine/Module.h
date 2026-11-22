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
    PrepareSpec (double rate = 44100.0, int maxBlock = 256, int channels = 2,
                 params::Quality q = params::Quality::Standard) noexcept
        : sampleRate (rate)
        , maxBlockSize (maxBlock)
        , numChannels (channels)
        , quality (q)
    {
    }

    double sampleRate = 44100.0;
    int maxBlockSize = 256; // the engine's chunk size, never the host's block size
    int numChannels = 2;
    params::Quality quality = params::Quality::Standard; // PRD FR-CORE-013
};

// What a module gets with every chunk. Modules read only their own parameters
// from the snapshot and smooth them themselves.
struct ProcessContext
{
    ProcessContext (const TransportState& t, const params::ParamSnapshot& p, const float* const* key = nullptr) noexcept
        : transport (t)
        , params (p)
        , dryKey (key)
    {
    }

    const TransportState& transport;
    const params::ParamSnapshot& params;

    // The dry signal, delayed by the whole chain's latency: it lines up with the
    // chain's output, where Duck sits and keys from it. Null where there is none
    // (a module tested on its own).
    const float* const* dryKey = nullptr;
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

    // False when reset() is too slow for the audio thread (it clears large
    // buffers; measured in tests/measure/ChainMeasure.cpp). A switched-off
    // ModuleSlot then lets such a module ring out on silence instead of resetting
    // it when it comes back on.
    virtual bool canResetInstantly() const noexcept { return true; }
};

} // namespace thunder
