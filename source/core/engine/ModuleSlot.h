#pragma once

#include "Module.h"

#include "core/util/Crossfade.h"
#include "core/util/LatencyDelay.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace thunder
{

// A module in the layer chain with its on/off switch (PRD FR-CORE-009,
// ARCHITECTURE §5.3).
//
// - Switching is click-free: an equal-power crossfade of ~20 ms between the
//   module's output and its input.
// - Latency stays constant (AD-02): while the module is off its input still
//   passes through a delay as long as the module's latency, so the host's delay
//   compensation and the dry path stay aligned.
// - A module that is fully off isn't processed at all (no CPU). When it is
//   switched back on it is reset first, so it starts clean rather than playing
//   whatever it held when it went off. A freshly reset module with latency
//   outputs silence until its first input comes through, so it runs unheard for
//   its latency before the fade begins; otherwise its signal would start with a
//   jump in the middle of the fade.
// - Some modules can't be reset on the audio thread: clearing the reverb's
//   delay lines takes ~10 ms, longer than a chunk (Module::canResetInstantly).
//   Such a module is never reset by the slot. Once it is switched off it keeps
//   running, unheard, on silence until its tail has died away (or for its tail
//   length plus a second at most), and only then rests. Switched back on, it
//   starts from that silence, or, if it comes back sooner, its own fading tail
//   continues naturally.
//
// The slot is itself a Module, so the engine runs it like any other (including
// the NaN/Inf fail-safe, which resets the module inside).
template<typename M> class ModuleSlot final : public Module
{
public:
    explicit ModuleSlot (params::ParamId enableParameter) noexcept
        : enableId (enableParameter)
    {
    }

    M& get() noexcept { return module; }
    const M& get() const noexcept { return module; }

    void prepare (const PrepareSpec& spec) override
    {
        module.prepare (spec);
        numChannels = std::clamp (spec.numChannels, 1, kMaxChannels);
        fade.prepare (spec.sampleRate, util::kBypassFadeSeconds);
        bypassDelay.prepare (spec.numChannels, module.getLatencySamples());

        for (auto& channel : bypassed)
            channel.assign (static_cast<std::size_t> (spec.maxBlockSize), 0.0f);
        for (auto& channel : silence)
            channel.assign (static_cast<std::size_t> (spec.maxBlockSize), 0.0f);

        drainLimit = static_cast<std::int64_t> ((module.getTailSeconds() + 1.0) * spec.sampleRate);

        reset();
    }

    void reset() noexcept override
    {
        module.reset();
        bypassDelay.reset();
        firstChunk = true;
        stale = false;
        resting = false;
        priming = 0;
        drainRemaining = 0;
    }

    void process (juce::dsp::AudioBlock<float>& block, const ProcessContext& context) noexcept override
    {
        const auto on = context.params.isOn (enableId);
        if (firstChunk)
        {
            fade.snapTo (on);
            firstChunk = false;
        }

        // Coming back on: start clean, and fill the latency before fading in.
        if (!on)
            priming = 0;
        if (on && stale)
        {
            module.reset();
            stale = false;
            priming = module.getLatencySamples();
        }
        fade.setTarget (on && priming <= 0);

        const auto channels = std::min (numChannels, static_cast<int> (block.getNumChannels()));
        const auto numSamples = static_cast<int> (block.getNumSamples());
        jassert (numSamples <= static_cast<int> (bypassed[0].size()));

        // The bypass signal: the input, delayed by the module's latency. Kept
        // running whenever the delay is needed, so it is ready when a fade starts.
        float* bypass[kMaxChannels] = {bypassed[0].data(), bypassed[1].data()};
        const auto delayed = bypassDelay.getDelaySamples() > 0;
        if (delayed || !fade.isFullyB())
        {
            for (int ch = 0; ch < channels; ++ch)
                juce::FloatVectorOperations::copy (bypass[ch], block.getChannelPointer (static_cast<std::size_t> (ch)),
                                                   numSamples);
            bypassDelay.process (bypass, channels, numSamples);
        }

        if (fade.isFullyA() && priming <= 0)
        {
            // Off: the module rests (after ringing out, if it can't be reset).
            if (!resting)
            {
                resting = true;
                drainRemaining = module.canResetInstantly() ? 0 : drainLimit;
            }
            if (module.canResetInstantly())
                stale = true;
            else
                drain (context, channels, numSamples);

            if (delayed)
                copyBack (block, bypass, channels, numSamples);
            return;
        }

        resting = false;
        module.process (block, context);

        if (priming > 0)
        {
            // Filling its latency, unheard.
            priming -= numSamples;
            copyBack (block, bypass, channels, numSamples);
            return;
        }

        if (fade.isFading())
        {
            float* processed[kMaxChannels] = {};
            for (int ch = 0; ch < channels; ++ch)
                processed[ch] = block.getChannelPointer (static_cast<std::size_t> (ch));

            fade.process (bypass, processed, channels, numSamples);
            copyBack (block, bypass, channels, numSamples);
        }
    }

    int getLatencySamples() const noexcept override { return module.getLatencySamples(); }
    double getTailSeconds() const noexcept override { return module.getTailSeconds(); }

    bool isActive() const noexcept { return !fade.isFullyA(); }

    // Whether a switched-off module that can't be reset is still ringing out.
    bool isDraining() const noexcept { return drainRemaining > 0; }

private:
    static constexpr int kMaxChannels = 2;

    // Below this (−120 dBFS) a ringing-out module counts as silent.
    static constexpr float kQuietLevel = 1.0e-6f;

    // [RT] Runs the switched-off module on silence until it has rung out.
    void drain (const ProcessContext& context, int channels, int numSamples) noexcept
    {
        if (drainRemaining <= 0)
            return;

        float* quiet[kMaxChannels] = {silence[0].data(), silence[1].data()};
        for (int ch = 0; ch < channels; ++ch)
            juce::FloatVectorOperations::clear (quiet[ch], numSamples);

        juce::dsp::AudioBlock<float> block (quiet, static_cast<std::size_t> (channels),
                                            static_cast<std::size_t> (numSamples));
        module.process (block, context);

        float peak = 0.0f;
        bool finite = true;
        for (int ch = 0; ch < channels; ++ch)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                finite = finite && std::isfinite (quiet[ch][i]);
                peak = std::max (peak, std::abs (quiet[ch][i]));
            }
        }

        if (!finite)
        {
            // Corrupted while unheard: stop, and accept a slow reset on the way
            // back on rather than playing noise.
            drainRemaining = 0;
            stale = true;
            return;
        }

        drainRemaining = peak < kQuietLevel ? 0 : drainRemaining - numSamples;
    }

    static void copyBack (juce::dsp::AudioBlock<float>& block, float* const* source, int channels,
                          int numSamples) noexcept
    {
        for (int ch = 0; ch < channels; ++ch)
            juce::FloatVectorOperations::copy (block.getChannelPointer (static_cast<std::size_t> (ch)), source[ch],
                                               numSamples);
    }

    M module;
    params::ParamId enableId;
    int numChannels = kMaxChannels;
    util::Crossfade fade; // A = bypassed, B = the module
    util::LatencyDelay bypassDelay;
    std::array<std::vector<float>, kMaxChannels> bypassed, silence;
    bool firstChunk = true;
    bool stale = false;              // off and not processed since: reset before the next use
    bool resting = false;            // fully off (since the last time it was heard)
    int priming = 0;                 // samples the module still runs unheard after a reset
    std::int64_t drainLimit = 0;     // the longest a module that can't be reset rings out
    std::int64_t drainRemaining = 0; // samples it may still ring out for
};

} // namespace thunder
