// A module's on/off switch: click-free crossfade, constant latency, no work
// while off, a clean start when switched back on (PRD FR-CORE-009,
// ARCHITECTURE §5.3; ROADMAP T-0.3-06).

#include "support/TestSignals.h"

#include "core/dsp/grains/GrainModule.h"
#include "core/engine/ModuleSlot.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace thunder;
using namespace thunder::test;
using Catch::Matchers::WithinAbs;
using params::ParamId;

namespace
{
constexpr double kRate = 48000.0;

// A stand-in module with latency: it outputs its input inverted and delayed by
// kDelay samples, and counts its calls.
class InvertingDelay final : public Module
{
public:
    static constexpr int kDelay = 100;

    void prepare (const PrepareSpec& spec) override
    {
        delay.prepare (spec.numChannels, kDelay);
        reset();
    }

    void reset() noexcept override
    {
        delay.reset();
        ++resets;
    }

    void process (juce::dsp::AudioBlock<float>& block, const ProcessContext&) noexcept override
    {
        ++processed;
        float* channels[] = {block.getChannelPointer (0), block.getChannelPointer (1)};
        delay.process (channels, 2, static_cast<int> (block.getNumSamples()));
        block.multiplyBy (-1.0f);
    }

    int getLatencySamples() const noexcept override { return kDelay; }

    util::LatencyDelay delay;
    int resets = 0;
    int processed = 0;
};

// Runs `input` through the slot in 256-sample chunks; the switch is read per
// chunk from `isOn(chunk start in samples)`.
template<typename M, typename IsOn>
std::vector<float> runSlot (ModuleSlot<M>& slot, const std::vector<float>& input, IsOn isOn,
                            params::ParamSnapshot snapshot = params::ParamSnapshot::defaults())
{
    const TransportState transport;
    std::vector<float> left = input, right = input;
    for (std::size_t start = 0; start < input.size(); start += 256)
    {
        const auto n = std::min<std::size_t> (256, input.size() - start);
        snapshot.set (ParamId::grainOn, isOn (start) ? 1.0f : 0.0f);
        float* channels[] = {left.data() + start, right.data() + start};
        juce::dsp::AudioBlock<float> block (channels, 2, n);
        slot.process (block, ProcessContext{transport, snapshot});
    }
    return left;
}

float largestStep (const std::vector<float>& s, std::size_t from, std::size_t to)
{
    float largest = 0.0f;
    for (auto i = std::max<std::size_t> (from, 1); i < to; ++i)
        largest = std::max (largest, std::abs (s[i] - s[i - 1]));
    return largest;
}
} // namespace

TEST_CASE ("A switched-off module passes its input, delayed by its latency, and does no work", "[unit][engine]")
{
    ModuleSlot<InvertingDelay> slot{ParamId::grainOn};
    slot.prepare ({kRate, 256, 2});
    CHECK (slot.getLatencySamples() == InvertingDelay::kDelay);

    const auto input = sine (440.0, kRate, 0.5);
    const auto out = runSlot (slot, input, [] (std::size_t) { return false; });

    CHECK (slot.get().processed == 0);
    CHECK_FALSE (slot.isActive());
    for (std::size_t i = InvertingDelay::kDelay; i < out.size(); ++i)
        REQUIRE (juce::exactlyEqual (out[i], input[i - InvertingDelay::kDelay]));
}

TEST_CASE ("A switched-on module is used as it is", "[unit][engine]")
{
    ModuleSlot<InvertingDelay> slot{ParamId::grainOn};
    slot.prepare ({kRate, 256, 2});

    const auto input = sine (440.0, kRate, 0.5);
    const auto out = runSlot (slot, input, [] (std::size_t) { return true; });

    CHECK (slot.isActive());
    for (std::size_t i = InvertingDelay::kDelay; i < out.size(); ++i)
        REQUIRE (juce::exactlyEqual (out[i], -input[i - InvertingDelay::kDelay]));
}

TEST_CASE ("Switching a module on and off crossfades over 20 ms and keeps the latency", "[unit][engine]")
{
    ModuleSlot<InvertingDelay> slot{ParamId::grainOn};
    slot.prepare ({kRate, 256, 2});

    // On, off at 0.5 s, on again at 1 s.
    const auto input = sine (220.0, kRate, 1.5);
    const auto offAt = static_cast<std::size_t> (0.5 * kRate) / 256 * 256;
    const auto onAt = static_cast<std::size_t> (1.0 * kRate) / 256 * 256;
    const auto out = runSlot (slot, input, [&] (std::size_t start) { return start < offAt || start >= onAt; });

    const auto fade = static_cast<std::size_t> (0.020 * kRate);
    const auto d = static_cast<std::size_t> (InvertingDelay::kDelay);

    // Switching back on, the reset module first fills its latency unheard (one
    // chunk here), then fades in.
    const auto fadeInAt = onAt + 256;

    // Settled on: the module's output; settled off: the delayed input. The
    // latency is the same either way. (A fade can take a sample or two more
    // than 20 ms: its steps are rounded in float.)
    for (std::size_t i = offAt - 1000; i < offAt; ++i)
        REQUIRE (juce::exactlyEqual (out[i], -input[i - d]));
    for (auto i = offAt + fade + 64; i < fadeInAt; ++i)
        REQUIRE (juce::exactlyEqual (out[i], input[i - d]));
    for (auto i = fadeInAt + fade + 64; i < out.size(); ++i)
        REQUIRE (juce::exactlyEqual (out[i], -input[i - d]));

    // No click at either switch: a sine flipping polarity through an
    // equal-power fade changes no faster than the sine itself plus the fade.
    const auto sineSlope = static_cast<float> (0.5 * kTwoPi * 220.0 / kRate);
    CHECK (largestStep (out, offAt - 10, offAt + fade + 10) < 1.2f * sineSlope + 0.002f);
    CHECK (largestStep (out, onAt - 10, fadeInAt + fade + 10) < 1.2f * sineSlope + 0.002f);

    // Switching back on reset the module first: once at prepare, once at the
    // slot's reset, once when it came back.
    CHECK (slot.get().resets == 3);
}

TEST_CASE ("Grains switch on and off without a click", "[unit][grains][engine]")
{
    ModuleSlot<dsp::GrainModule> slot{ParamId::grainOn};
    slot.prepare ({kRate, 256, 2});
    CHECK (slot.getLatencySamples() == 0);

    // Defaults, Amount 100%: a sine and its grain cloud are unrelated in phase,
    // so a hard switch would jump.
    auto snapshot = params::ParamSnapshot::defaults();
    snapshot.set (ParamId::grainAmount, 100.0f);

    const auto input = sine (330.0, kRate, 3.0);
    const auto period = static_cast<std::size_t> (0.4 * kRate);
    const auto out = runSlot (slot, input, [&] (std::size_t start) { return (start / period) % 2 == 0; }, snapshot);

    CHECK (allFinite (out));

    // Around each switch the output moves no faster than the cloud or the
    // input do on their own (plus the fade's own slope): no click.
    const auto fade = static_cast<std::size_t> (0.020 * kRate);
    float steady = static_cast<float> (0.5 * kTwoPi * 330.0 / kRate); // the input's steepest
    for (std::size_t on = 0; on < out.size(); on += 2 * period)
        steady = std::max (steady, largestStep (out, on + fade + 300, std::min (out.size(), on + period - 300)));

    for (auto at = period; at < out.size(); at += period)
    {
        INFO ("switch at " << at);
        const auto chunkStart = at / 256 * 256;
        const auto from = chunkStart > 10 ? chunkStart - 10 : 1;
        CHECK (largestStep (out, from, std::min (out.size(), chunkStart + 256 + fade + 10)) < 1.25f * steady + 0.005f);
    }

    // While off, the input passes untouched.
    const auto offFrom = period + static_cast<std::size_t> (0.03 * kRate);
    for (auto i = offFrom; i < 2 * period - 256; ++i)
        REQUIRE (juce::exactlyEqual (out[i], input[i]));
}

namespace
{
// A stand-in for the reverb: a slow-decaying echo that can't be reset on the
// audio thread. It counts its calls.
class SlowReset final : public Module
{
public:
    void prepare (const PrepareSpec&) override { reset(); }
    void reset() noexcept override
    {
        state = {};
        ++resets;
    }

    void process (juce::dsp::AudioBlock<float>& block, const ProcessContext&) noexcept override
    {
        ++processed;
        for (std::size_t ch = 0; ch < 2; ++ch)
        {
            auto* x = block.getChannelPointer (ch);
            for (std::size_t i = 0; i < block.getNumSamples(); ++i)
            {
                state[ch] = 0.999f * state[ch] + 0.05f * x[i]; // ~14k samples to fall 120 dB
                x[i] = state[ch];
            }
        }
    }

    double getTailSeconds() const noexcept override { return 1.0; }
    bool canResetInstantly() const noexcept override { return false; }

    std::array<float, 2> state{};
    int resets = 0;
    int processed = 0;
};
} // namespace

TEST_CASE ("A module that can't be reset rings out on silence when switched off", "[unit][engine]")
{
    ModuleSlot<SlowReset> slot{ParamId::grainOn};
    slot.prepare ({kRate, 256, 2});
    const auto resetsAfterPrepare = slot.get().resets;

    // On for 0.25 s, off for 1.5 s, on again.
    const auto input = sine (220.0, kRate, 2.0);
    const auto offAt = static_cast<std::size_t> (0.25 * kRate) / 256 * 256;
    const auto onAt = static_cast<std::size_t> (1.75 * kRate) / 256 * 256;

    int processedWhenSettled = -1;
    const auto out = runSlot (slot, input,
                              [&] (std::size_t start)
                              {
                                  // Half a second before switching back on, it has rung out.
                                  if (start == onAt - static_cast<std::size_t> (0.5 * kRate) / 256 * 256)
                                      processedWhenSettled = slot.get().processed;
                                  return start < offAt || start >= onAt;
                              });

    // It kept running after the switch-off, unheard, then stopped once silent:
    // ~14k samples of ringing is about 55 chunks, far fewer than the 1.5 s off.
    const auto chunksWhileOn = static_cast<int> (offAt / 256);
    REQUIRE (processedWhenSettled > chunksWhileOn + 4);
    CHECK (processedWhenSettled < chunksWhileOn + 4 + 80);
    CHECK_FALSE (slot.isDraining());

    // Never reset by the slot, not even when switched back on.
    CHECK (slot.get().resets == resetsAfterPrepare);

    // While off, the input passes untouched.
    for (auto i = offAt + 2000; i < onAt; ++i)
        REQUIRE (juce::exactlyEqual (out[i], input[i]));
    CHECK (allFinite (out));
}
