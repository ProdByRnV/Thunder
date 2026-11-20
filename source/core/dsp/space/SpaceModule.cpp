#include "SpaceModule.h"

#include <DSP/ReverbController.h>

#include <algorithm>
#include <cmath>

namespace thunder::dsp
{

using params::ParamId;
namespace CP = Cloudseed::Parameter;

static_assert (SpaceModule::kReverbBlock == BUFFER_SIZE, "CloudSeed is built for blocks of kReverbBlock samples");

namespace
{
constexpr double kMinDecaySeconds = 0.4;
constexpr double kMaxDecaySeconds = 20.0;
constexpr double kMaxDampingHz = 18000.0; // damping 0%
constexpr double kMinDampingHz = 1000.0;  // damping 100%

// The settings change in steps per chunk, so they glide a little more slowly
// than the default to keep each step small.
constexpr double kSettingsRampSeconds = 0.1;

// Thunder's "Cloud" character, as CloudSeed's normalised (0..1) parameter
// values. Decay, pre-delay and damping are set from Thunder's parameters.
struct CloudSetting
{
    int id;
    double value;
};

// clang-format off
constexpr CloudSetting kCloudSettings[] = {
    {CP::Interpolation,         1.0},
    {CP::InputMix,              0.25},  // a little left/right cross-feed: wider, more even
    {CP::LowCutEnabled,         1.0},
    {CP::LowCut,                0.5},   // ≈ 215 Hz: keeps the reverb out of the low end
    {CP::HighCutEnabled,        1.0},
    {CP::HighCut,               0.85},  // ≈ 12.9 kHz
    {CP::DryOut,                0.0},   // muted: the module mixes dry and wet itself
    {CP::EarlyOut,              0.7},   // -9 dB
    {CP::LateOut,               1.0},   // 0 dB

    {CP::TapEnabled,            0.0},   // no discrete echoes; pre-delay still applies
    {CP::TapCount,              0.2},
    {CP::TapDecay,              1.0},
    {CP::TapLength,             0.98},

    {CP::EarlyDiffuseEnabled,   1.0},
    {CP::EarlyDiffuseCount,     0.4},   // 5 stages
    {CP::EarlyDiffuseDelay,     0.3},
    {CP::EarlyDiffuseModAmount, 0.2},
    {CP::EarlyDiffuseFeedback,  0.7},
    {CP::EarlyDiffuseModRate,   0.25},

    {CP::LateMode,              1.0},   // taps after the diffusers: smoother
    {CP::LateLineCount,         0.6},   // 8 delay lines
    {CP::LateDiffuseEnabled,    1.0},
    {CP::LateDiffuseCount,      0.5},
    {CP::LateLineSize,          0.55},  // ≈ 135 ms: a large space
    {CP::LateLineModAmount,     0.3},
    {CP::LateDiffuseDelay,      0.25},
    {CP::LateDiffuseModAmount,  0.2},
    {CP::LateLineModRate,       0.25},
    {CP::LateDiffuseFeedback,   0.8},
    {CP::LateDiffuseModRate,    0.2},

    {CP::EqLowShelfEnabled,     0.0},
    {CP::EqHighShelfEnabled,    1.0},
    {CP::EqLowpassEnabled,      1.0},   // the damping filter
    {CP::EqLowFreq,             0.4},
    {CP::EqHighFreq,            0.5},
    {CP::EqLowGain,             0.55},
    {CP::EqHighGain,            0.75},  // -5 dB high shelf in the tail
    {CP::EqCrossSeed,           0.0},   // left and right fully decorrelated

    {CP::SeedTap,               0.334},
    {CP::SeedDiffusion,         0.185},
    {CP::SeedDelay,             0.218},
    {CP::SeedPostDiffusion,     0.365},
};
// clang-format on

// CloudSeed takes 0..1 values and maps them onto real units with its own
// curves (Parameters.h, ScaleParam). These invert those curves so Thunder can
// ask for seconds, milliseconds and hertz directly.
double normalisedLineDecay (double seconds) // 0.05 + Resp3dec(x) · 59.95
{
    const auto r = std::clamp ((seconds - 0.05) / 59.95, 0.0, 1.0);
    return std::log10 (r * 999.0 + 1.0) / 3.0;
}

double normalisedPreDelay (double milliseconds) // Resp1dec(x) · 500
{
    const auto r = std::clamp (milliseconds / 500.0, 0.0, 1.0);
    return std::log10 (r * 9.0 + 1.0);
}

double normalisedEqCutoff (double hertz) // 400 + Resp4oct(x) · 19600
{
    const auto r = std::clamp ((hertz - 400.0) / 19600.0, 0.0, 1.0);
    return std::log2 (r * 15.0 + 1.0) / 4.0;
}
} // namespace

SpaceModule::SpaceModule() = default;
SpaceModule::~SpaceModule() = default;

double SpaceModule::decaySeconds (float decayPercent) noexcept
{
    const auto t = std::clamp (static_cast<double> (decayPercent) / 100.0, 0.0, 1.0);
    return kMinDecaySeconds * std::pow (kMaxDecaySeconds / kMinDecaySeconds, t);
}

double SpaceModule::dampingCutoffHz (float dampingPercent) noexcept
{
    const auto t = std::clamp (static_cast<double> (dampingPercent) / 100.0, 0.0, 1.0);
    return kMaxDampingHz * std::pow (kMinDampingHz / kMaxDampingHz, t);
}

void SpaceModule::prepare (const PrepareSpec& spec)
{
    const auto sampleRate = static_cast<int> (std::lround (spec.sampleRate));
    sampleRateHz = spec.sampleRate;

    // CloudSeed allocates its delay memory up front, once.
    if (reverb == nullptr)
    {
        // Every reverb starts its modulation from the same phases, so every
        // instance (and every reopened session) sounds the same. See
        // cmake/compat/CloudSeedDeterministic.h.
        Cloudseed::ThunderPhaseSeed() = 1;
        reverb = std::make_unique<Cloudseed::ReverbController> (sampleRate);
        for (const auto& setting : kCloudSettings)
            reverb->SetParameter (setting.id, setting.value);
    }

    if (sampleRate != preparedSampleRate)
    {
        reverb->SetSamplerate (sampleRate);
        preparedSampleRate = sampleRate;
    }

    for (auto& channel : wet)
        channel.assign (static_cast<std::size_t> (spec.maxBlockSize), 0.0f);

    decay.prepare (spec.sampleRate, kSettingsRampSeconds);
    preDelayMs.prepare (spec.sampleRate, kSettingsRampSeconds);
    damping.prepare (spec.sampleRate, kSettingsRampSeconds);
    mix.prepare (spec.sampleRate, util::kDefaultRampSeconds);

    // Quiet for longer than the longest pre-delay plus one reverb block means
    // nothing is still travelling through the delay lines.
    quietThreshold = static_cast<std::int64_t> (
                         std::ceil (params::paramDef (ParamId::spacePreDelay).max / 1000.0 * spec.sampleRate)) +
                     2 * kReverbBlock;

    // prepare() runs off the audio thread: always clear here.
    quietSamples = 0;
    reset();
}

void SpaceModule::reset() noexcept
{
    // Clearing CloudSeed's delay lines takes 7–10 ms (they are sized for 2 s
    // at 192 kHz whatever the rate). When the reverb has been quiet long enough
    // there is nothing to clear, which is the usual case: tails die out while
    // the transport is stopped (PRD DEC-043).
    // Either way the modulation returns to its start (Thunder's CloudSeed patch),
    // so a reset reverb sounds like a fresh one.
    if (reverb != nullptr)
    {
        Cloudseed::ThunderSkipDelayZeroing() = isQuiet();
        reverb->ClearBuffers();
        Cloudseed::ThunderSkipDelayZeroing() = false;
    }
    quietSamples = quietThreshold;

    for (auto* buffers : {&reverbIn, &reverbOut})
        for (auto& channel : *buffers)
            channel.fill (0.0f);
    blockPosition = 0;

    firstChunk = true;
}

double SpaceModule::getTailSeconds() const noexcept
{
    // The longest possible tail, whatever the current settings: a host that
    // reads the tail once and keeps it would otherwise cut a longer reverb short
    // (PRD DEC-043). Constant, so it is safe to read from any thread.
    return kMaxDecaySeconds + params::paramDef (ParamId::spacePreDelay).max / 1000.0;
}

void SpaceModule::applyReverbSettings (bool force) noexcept
{
    // Only settings whose CloudSeed update is allocation-free are changed while
    // playing. (Thunder builds CloudSeed with a small patch that makes the
    // decay update allocation-free; see cmake/CloudSeed.cmake.)
    if (force || !juce::exactlyEqual (decay.current(), appliedDecay))
    {
        appliedDecay = decay.current();
        reverb->SetParameter (CP::LateLineDecay, normalisedLineDecay (decaySeconds (appliedDecay)));
    }

    if (force || !juce::exactlyEqual (preDelayMs.current(), appliedPreDelay))
    {
        appliedPreDelay = preDelayMs.current();
        // The fixed-block buffering already delays the wet signal by one block.
        const auto blockMs = 1000.0 * kReverbBlock / sampleRateHz;
        reverb->SetParameter (CP::TapPredelay, normalisedPreDelay (std::max (0.0, appliedPreDelay - blockMs)));
    }

    if (force || !juce::exactlyEqual (damping.current(), appliedDamping))
    {
        appliedDamping = damping.current();
        reverb->SetParameter (CP::EqCutoff, normalisedEqCutoff (dampingCutoffHz (appliedDamping)));
    }
}

void SpaceModule::process (juce::dsp::AudioBlock<float>& block, const ProcessContext& context) noexcept
{
    const auto numSamples = static_cast<int> (block.getNumSamples());
    if (numSamples == 0 || reverb == nullptr)
        return;

    jassert (static_cast<std::size_t> (numSamples) <= wet[0].size());
    jassert (block.getNumChannels() >= 2);

    const auto& p = context.params;
    const auto mixTarget = p.get (ParamId::spaceMix) / 100.0f;

    if (firstChunk)
    {
        decay.snapTo (p.get (ParamId::spaceDecay));
        preDelayMs.snapTo (p.get (ParamId::spacePreDelay));
        damping.snapTo (p.get (ParamId::spaceDamping));
        mix.snapTo (mixTarget);
        applyReverbSettings (true);
        firstChunk = false;
    }
    else
    {
        decay.setTarget (p.get (ParamId::spaceDecay));
        preDelayMs.setTarget (p.get (ParamId::spacePreDelay));
        damping.setTarget (p.get (ParamId::spaceDamping));
        mix.setTarget (mixTarget);
    }

    auto* left = block.getChannelPointer (0);
    auto* right = block.getChannelPointer (1);

    // CloudSeed always gets whole blocks of kReverbBlock samples: its delay
    // lines feed back one block late, so other block sizes would change the
    // sound (and an offline bounce would differ from playback). The wet signal
    // comes out exactly kReverbBlock samples later, which the pre-delay
    // compensates for.
    for (int done = 0; done < numSamples;)
    {
        const auto n = std::min (kReverbBlock - blockPosition, numSamples - done);
        const auto from = static_cast<std::size_t> (done);
        const auto at = static_cast<std::size_t> (blockPosition);

        std::copy_n (left + done, n, reverbIn[0].data() + at);
        std::copy_n (right + done, n, reverbIn[1].data() + at);
        std::copy_n (reverbOut[0].data() + at, n, wet[0].data() + from);
        std::copy_n (reverbOut[1].data() + at, n, wet[1].data() + from);

        blockPosition += n;
        done += n;

        if (blockPosition == kReverbBlock)
        {
            decay.skip (kReverbBlock);
            preDelayMs.skip (kReverbBlock);
            damping.skip (kReverbBlock);
            applyReverbSettings (false);

            reverb->Process (reverbIn[0].data(), reverbIn[1].data(), reverbOut[0].data(), reverbOut[1].data(),
                             kReverbBlock);
            blockPosition = 0;
        }
    }

    // Equal-power wet/dry: the reverb is uncorrelated with the dry signal, so
    // this keeps the overall level steady as Mix moves. On the way, the
    // loudest input or reverb sample, for the quiet tracking.
    float loudest = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        const auto angle = mix.next() * juce::MathConstants<float>::halfPi;
        const auto dryGain = std::cos (angle);
        const auto wetGain = std::sin (angle);
        const auto index = static_cast<std::size_t> (i);

        loudest = std::max (
            {loudest, std::abs (left[i]), std::abs (right[i]), std::abs (wet[0][index]), std::abs (wet[1][index])});

        left[i] = left[i] * dryGain + wet[0][index] * wetGain;
        right[i] = right[i] * dryGain + wet[1][index] * wetGain;
    }

    quietSamples = loudest < kQuietLevel ? std::min (quietSamples + numSamples, quietThreshold) : 0;
}

} // namespace thunder::dsp
