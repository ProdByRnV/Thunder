#pragma once

#include "SourceBuffer.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <functional>
#include <memory>

// Loading sources from files and from the copy saved in a project (PRD
// FR-INPUT-001/002/004, ARCHITECTURE §7.3). Background thread only: these
// read files and allocate freely.

namespace thunder::source
{

// The longest source Thunder loads. A source is kept in memory as 32-bit
// float at its own rate: 5 minutes of stereo at 48 kHz is about 110 MB.
inline constexpr double kMaxSeconds = 300.0;

// The largest copy of a source saved inside a project (PRD OQ-05): 4 MB of
// FLAC, roughly 30–45 s of stereo audio. Longer sources are saved by path only.
inline constexpr std::size_t kMaxEmbeddedBytes = 4u * 1024u * 1024u;

enum class LoadError
{
    None,
    NotFound,     // the file isn't there
    NotSupported, // not a WAV, AIFF, FLAC, MP3 or OGG file
    Unreadable,   // the right kind of file, but it couldn't be decoded
    TooLong,      // longer than kMaxSeconds
    Empty,        // no audio in it
    Cancelled     // a newer request replaced this one
};

struct LoadResult
{
    std::shared_ptr<SourceBuffer> buffer; // null unless error == None
    LoadError error = LoadError::None;
};

// Called while decoding with the fraction done (0 … 1); return false to stop.
using Progress = std::function<bool (float)>;

// The file types Thunder loads, by extension.
bool isSupportedFile (const juce::File& file);

// "WAV, AIFF, FLAC, MP3 or OGG".
juce::String supportedTypesText();

// [BG] Decodes a file into a new SourceBuffer (audio only; see finishSource).
LoadResult loadFile (const juce::File& file, const Progress& progress = {});

// [BG] Decodes the copy saved with a project.
LoadResult loadEmbedded (const juce::MemoryBlock& data, const Progress& progress = {});

// [BG] The project copy of a source: FLAC, 16-bit, or null if it would be
// larger than kMaxEmbeddedBytes.
std::shared_ptr<const juce::MemoryBlock> encodeEmbedded (const SourceBuffer& source);

// [BG] Works out what the UI shows (peaks) and the source's key.
void finishSource (SourceBuffer& source);

// A short sentence for the UI.
juce::String describe (LoadError error);

} // namespace thunder::source
