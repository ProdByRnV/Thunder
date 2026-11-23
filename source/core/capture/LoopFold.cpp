#include "LoopFold.h"

#include <algorithm>
#include <cmath>

namespace thunder::capture
{

namespace
{
constexpr double kCompareSeconds = 0.5;
constexpr double kFadeSeconds = 0.01;      // the folded tail's end
constexpr double kCrossfadeSeconds = 0.05; // a replay back into the original start
constexpr int kCopyBlock = 65536;

double correlation (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int frames)
{
    double ab = 0.0, aa = 0.0, bb = 0.0;
    for (int ch = 0; ch < std::min (a.getNumChannels(), b.getNumChannels()); ++ch)
    {
        const auto* x = a.getReadPointer (ch);
        const auto* y = b.getReadPointer (ch);
        for (int i = 0; i < frames; ++i)
        {
            ab += static_cast<double> (x[i]) * y[i];
            aa += static_cast<double> (x[i]) * x[i];
            bb += static_cast<double> (y[i]) * y[i];
        }
    }
    return aa > 0.0 && bb > 0.0 ? ab / std::sqrt (aa * bb) : 0.0;
}
} // namespace

FoldKind foldTail (juce::AudioBuffer<float>& head, std::int64_t loopFrames, const juce::AudioBuffer<float>& tail,
                   double sampleRate)
{
    const auto tailFrames = tail.getNumSamples();
    if (tailFrames == 0 || loopFrames <= 0)
        return FoldKind::none;

    const auto loop = static_cast<int> (std::min<std::int64_t> (loopFrames, head.getNumSamples()));
    jassert (loop >= std::min<std::int64_t> (loopFrames, tailFrames));
    const auto channels = std::min (head.getNumChannels(), tail.getNumChannels());

    const auto compare = std::min ({loop, tailFrames, static_cast<int> (kCompareSeconds * sampleRate)});
    if (compare > 0 && correlation (head, tail, compare) >= kReplayCorrelation)
    {
        // A replay: what followed the last bar replaces the start, then
        // crossfades (equal gain: the two are alike) back into the original.
        const auto length = std::min (loop, tailFrames);
        const auto fade = std::min (length, std::max (1, static_cast<int> (kCrossfadeSeconds * sampleRate)));
        for (int ch = 0; ch < channels; ++ch)
        {
            auto* out = head.getWritePointer (ch);
            const auto* in = tail.getReadPointer (ch);
            for (int i = 0; i < length; ++i)
            {
                const auto towardsOriginal =
                    i < length - fade ? 0.0f
                                      : static_cast<float> (i - (length - fade) + 1) / static_cast<float> (fade + 1);
                out[i] = in[i] * (1.0f - towardsOriginal) + out[i] * towardsOriginal;
            }
        }
        return FoldKind::replaced;
    }

    // A tail: added onto the start, wrapping round a short loop, fading out at its end.
    const auto fade = std::min (tailFrames, std::max (1, static_cast<int> (kFadeSeconds * sampleRate)));
    for (int ch = 0; ch < channels; ++ch)
    {
        auto* out = head.getWritePointer (ch);
        const auto* in = tail.getReadPointer (ch);
        for (int i = 0; i < tailFrames; ++i)
        {
            const auto gain =
                i < tailFrames - fade ? 1.0f : static_cast<float> (tailFrames - i) / static_cast<float> (fade + 1);
            out[i % loop] += in[i] * gain;
        }
    }
    return FoldKind::added;
}

bool foldLoopFile (const juce::File& file, std::int64_t loopFrames, CaptureFormat format, FoldKind* kind)
{
    std::unique_ptr<juce::AudioFormatReader> reader (
        juce::WavAudioFormat().createReaderFor (file.createInputStream().release(), true));
    if (reader == nullptr || loopFrames <= 0)
        return false;

    const auto total = reader->lengthInSamples;
    const auto loop = std::min<std::int64_t> (loopFrames, total);
    const auto tailFrames = static_cast<int> (total - loop);
    const auto channels = static_cast<int> (reader->numChannels);
    const auto rate = reader->sampleRate;

    // The start (as much of it as the tail covers) and the tail, in memory: at
    // most 10 s each.
    const auto headFrames = static_cast<int> (std::min<std::int64_t> (loop, std::max (tailFrames, 1)));
    juce::AudioBuffer<float> head (channels, headFrames);
    juce::AudioBuffer<float> tail (channels, std::max (tailFrames, 0));
    if (!reader->read (&head, 0, headFrames, 0, true, true))
        return false;
    if (tailFrames > 0 && !reader->read (&tail, 0, tailFrames, loop, true, true))
        return false;

    const auto folded = foldTail (head, loop, tail, rate);
    if (kind != nullptr)
        *kind = folded;

    // Written beside the file, then put in its place.
    const auto temp = file.getSiblingFile (file.getFileName() + ".fold");
    {
        std::unique_ptr<juce::OutputStream> stream = temp.createOutputStream();
        if (stream == nullptr)
            return false;

        using Options = juce::AudioFormatWriterOptions;
        const auto isFloat = format == CaptureFormat::Float32;
        const auto options =
            Options{}
                .withSampleRate (rate)
                .withNumChannels (channels)
                .withBitsPerSample (isFloat ? 32 : 24)
                .withSampleFormat (isFloat ? Options::SampleFormat::floatingPoint : Options::SampleFormat::integral);
        auto writer = juce::WavAudioFormat().createWriterFor (stream, options);
        if (writer == nullptr)
        {
            temp.deleteFile();
            return false;
        }

        auto ok = writer->writeFromAudioSampleBuffer (head, 0, headFrames);
        juce::AudioBuffer<float> block (channels, kCopyBlock);
        for (std::int64_t position = headFrames; ok && position < loop; position += kCopyBlock)
        {
            const auto n = static_cast<int> (std::min<std::int64_t> (kCopyBlock, loop - position));
            ok = reader->read (&block, 0, n, position, true, true) && writer->writeFromAudioSampleBuffer (block, 0, n);
        }
        ok = ok && writer->flush();
        writer.reset();
        if (!ok)
        {
            temp.deleteFile();
            return false;
        }
    }

    reader.reset();
    if (!temp.moveFileTo (file))
    {
        temp.deleteFile();
        return false;
    }
    return true;
}

} // namespace thunder::capture
