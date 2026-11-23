#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <cstdint>
#include <memory>

namespace thunder::source
{

// What a project remembers about its source (PRD §9.12, FR-INPUT-004): the
// file's path and, when it is small enough, a copy of the audio, so the source
// survives the file being moved or the project being opened elsewhere.
//
//   <Source path="…" gridOffsetSamples="0" captured="1" bpm="140" bars="4"
//           timeSigNumerator="4" timeSigDenominator="4" embedded="…base64 FLAC…"/>
struct SourceRef
{
    juce::String path;
    std::int64_t gridOffsetSamples = 0;
    bool fromCapture = false;
    double bpm = 0.0; // 0 = not known
    int bars = 0;
    int timeSigNumerator = 4;
    int timeSigDenominator = 4;
    std::shared_ptr<const juce::MemoryBlock> embedded; // null = path only

    bool isEmpty() const noexcept { return path.isEmpty() && embedded == nullptr; }

    // The same source (loading it again would change nothing).
    bool sameSourceAs (const SourceRef& other) const noexcept;

    // An invalid tree when empty.
    juce::ValueTree toValueTree() const;

    // Missing or damaged values fall back to defaults; no Source child = empty.
    static SourceRef fromValueTree (const juce::ValueTree& tree);
};

} // namespace thunder::source
