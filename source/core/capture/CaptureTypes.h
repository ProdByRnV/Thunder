#pragma once

#include "core/state/SessionState.h"

#include <array>
#include <cstdint>
#include <type_traits>

// Shared by the audio-thread side of capture (CaptureController) and the
// background writer (CaptureWriter): everything they exchange travels through
// one lock-free queue of CaptureBlocks, in order (ARCHITECTURE §8).

namespace thunder::capture
{

using state::CaptureSource;

// The WAV sample format (PRD FR-CAPT-005; a global user setting).
enum class CaptureFormat
{
    Int24,
    Float32
};

// Why a capture didn't produce a file.
enum class CaptureFailure
{
    None,
    TransportStopped, // playback stopped before the last bar was recorded
    TransportJumped,  // playback moved somewhere that breaks the bar grid
    Restarted,        // the host reset the plugin mid-capture
    TooSlow,          // the writer fell behind and the queue filled up
    CannotCreateFile, // the captures folder or the file couldn't be created
    WriteFailed       // writing failed part-way (e.g. the disk is full)
};

// What the audio thread knows about a capture when its first bar starts.
struct CaptureStart
{
    double sampleRate = 0.0;
    double bpm = 0.0;
    int timeSigNumerator = 4;
    int timeSigDenominator = 4;
    int bars = 0;
    CaptureSource source = CaptureSource::Layer;
    int keyRoot = 0;                 // params::KeyRoot index
    int keyScale = 0;                // params::Scale index
    std::int64_t expectedFrames = 0; // bars at the starting tempo; for progress only
};

// One item in the capture queue. Audio is batched into full blocks so the
// queue holds the same number of seconds whatever the host's block size.
struct CaptureBlock
{
    static constexpr int kMaxFrames = 256;

    enum class Kind
    {
        Start,  // `start` is valid; audio blocks follow
        Audio,  // `numFrames` frames of stereo audio
        End,    // the capture is complete
        Failed, // `failure` says why; discard the capture
        Cancelled
    };

    std::uint32_t captureId = 0;
    Kind kind = Kind::Audio;
    int numFrames = 0;
    CaptureFailure failure = CaptureFailure::None;
    CaptureStart start;
    std::array<std::array<float, kMaxFrames>, 2> audio{};
};

static_assert (std::is_trivially_copyable_v<CaptureBlock>);

} // namespace thunder::capture
