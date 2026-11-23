#include "SourceLoader.h"

#include <algorithm>
#include <cmath>

namespace thunder::source
{

namespace
{
constexpr int kReadBlock = 65536;

// The formats Thunder reads, the same on every OS (no system codecs, so a
// file that loads on one computer loads on all of them). One per load: they
// are cheap, and instances load on their own threads.
struct Formats
{
    Formats()
    {
        manager.registerFormat (new juce::WavAudioFormat(), true);
        manager.registerFormat (new juce::AiffAudioFormat(), false);
        manager.registerFormat (new juce::FlacAudioFormat(), false);
        manager.registerFormat (new juce::OggVorbisAudioFormat(), false);
        manager.registerFormat (new juce::MP3AudioFormat(), false);
    }

    juce::AudioFormatManager manager;
};

LoadResult read (juce::AudioFormatReader& reader, const Progress& progress)
{
    if (reader.sampleRate <= 0.0 || reader.numChannels == 0)
        return {nullptr, LoadError::Unreadable};

    const auto length = reader.lengthInSamples;
    if (length <= 0)
        return {nullptr, LoadError::Empty};
    if (static_cast<double> (length) / reader.sampleRate > kMaxSeconds)
        return {nullptr, LoadError::TooLong};

    auto source = std::make_shared<SourceBuffer>();
    source->sampleRate = reader.sampleRate;
    const auto channels = static_cast<int> (std::min (2u, reader.numChannels));
    source->audio.setSize (channels, static_cast<int> (length));

    for (juce::int64 start = 0; start < length; start += kReadBlock)
    {
        const auto n = static_cast<int> (std::min<juce::int64> (kReadBlock, length - start));
        if (!reader.read (&source->audio, static_cast<int> (start), n, start, true, channels > 1))
            return {nullptr, LoadError::Unreadable};

        if (progress && !progress (static_cast<float> (static_cast<double> (start + n) / static_cast<double> (length))))
            return {nullptr, LoadError::Cancelled};
    }

    // Damaged files can decode to NaN; nothing downstream should see it.
    for (int ch = 0; ch < channels; ++ch)
    {
        auto* data = source->audio.getWritePointer (ch);
        for (int i = 0; i < source->audio.getNumSamples(); ++i)
            if (!std::isfinite (data[i]))
                data[i] = 0.0f;
    }

    return {std::move (source), LoadError::None};
}
} // namespace

bool isSupportedFile (const juce::File& file)
{
    return file.hasFileExtension ("wav;wave;aif;aiff;aifc;flac;mp3;ogg");
}

juce::String supportedTypesText()
{
    return "WAV, AIFF, FLAC, MP3 or OGG";
}

LoadResult loadFile (const juce::File& file, const Progress& progress)
{
    if (!file.existsAsFile())
        return {nullptr, LoadError::NotFound};
    if (!isSupportedFile (file))
        return {nullptr, LoadError::NotSupported};

    Formats formats;
    const std::unique_ptr<juce::AudioFormatReader> reader (formats.manager.createReaderFor (file));
    if (reader == nullptr)
        return {nullptr, LoadError::Unreadable};

    auto result = read (*reader, progress);
    if (result.buffer != nullptr)
    {
        result.buffer->file = file;
        result.buffer->name = file.getFileName();
    }
    return result;
}

LoadResult loadEmbedded (const juce::MemoryBlock& data, const Progress& progress)
{
    if (data.isEmpty())
        return {nullptr, LoadError::Empty};

    const std::unique_ptr<juce::AudioFormatReader> reader (
        juce::FlacAudioFormat().createReaderFor (new juce::MemoryInputStream (data, false), true));
    if (reader == nullptr)
        return {nullptr, LoadError::Unreadable};

    return read (*reader, progress);
}

std::shared_ptr<const juce::MemoryBlock> encodeEmbedded (const SourceBuffer& source)
{
    const auto frames = source.audio.getNumSamples();
    const auto channels = source.audio.getNumChannels();
    if (frames == 0 || channels == 0 || source.sampleRate <= 0.0)
        return nullptr;

    // FLAC rarely gets 16-bit music below a third of its size: don't even try
    // what can't fit.
    if (static_cast<std::size_t> (frames) * static_cast<std::size_t> (channels) * 2u > 3u * kMaxEmbeddedBytes)
        return nullptr;

    auto block = std::make_shared<juce::MemoryBlock>();
    {
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream> (*block, false);
        const auto options = juce::AudioFormatWriterOptions{}
                                 .withSampleRate (source.sampleRate)
                                 .withNumChannels (channels)
                                 .withBitsPerSample (16);
        auto writer = juce::FlacAudioFormat().createWriterFor (stream, options);
        if (writer == nullptr)
            return nullptr;
        if (!writer->writeFromAudioSampleBuffer (source.audio, 0, frames))
            return nullptr;
    } // the writer finishes the stream when deleted

    if (block->getSize() > kMaxEmbeddedBytes)
        return nullptr;
    return block;
}

void finishSource (SourceBuffer& source)
{
    const auto frames = source.audio.getNumSamples();
    source.peaks.assign (SourceBuffer::kPeakCount, 0.0f);
    if (frames > 0)
    {
        for (int slice = 0; slice < SourceBuffer::kPeakCount; ++slice)
        {
            const auto from = static_cast<int> (static_cast<juce::int64> (frames) * slice / SourceBuffer::kPeakCount);
            const auto to = std::max (from + 1, static_cast<int> (static_cast<juce::int64> (frames) * (slice + 1) /
                                                                  SourceBuffer::kPeakCount));
            float peak = 0.0f;
            for (int ch = 0; ch < source.audio.getNumChannels(); ++ch)
                peak = std::max (peak, source.audio.getMagnitude (ch, from, std::min (to, frames) - from));
            source.peaks[static_cast<std::size_t> (slice)] = peak;
        }
    }

    source.key = analysis::KeyDetector::analyse (source.audio, source.sampleRate);
}

juce::String describe (LoadError error)
{
    switch (error)
    {
        case LoadError::None:
        case LoadError::Cancelled:
            return {};
        case LoadError::NotFound:
            return "The source file couldn't be found.";
        case LoadError::NotSupported:
            return "That file type isn't supported. Thunder loads " + supportedTypesText() + " files.";
        case LoadError::Unreadable:
            return "That file couldn't be read. It may be damaged.";
        case LoadError::TooLong:
            return "That file is longer than 5 minutes, the most Thunder loads.";
        case LoadError::Empty:
            return "That file has no audio in it.";
    }
    return {};
}

} // namespace thunder::source
