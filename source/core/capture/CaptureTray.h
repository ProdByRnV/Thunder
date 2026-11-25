#pragma once

#include "core/state/SessionState.h"

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include <vector>

// The capture tray (PRD FR-CAPT-011): the project's last captures, newest
// first, each still draggable into the DAW. Stored in the project's session
// (never in presets) as
//
//   <CaptureTray>
//     <Capture file="…/Thunder_….wav" bars="4" source="layer" tail="0" loop="1"/>
//   </CaptureTray>
//
// A capture whose file is gone drops out.

namespace thunder::capture
{

struct TrayEntry
{
    juce::File file;
    int bars = 0;
    state::CaptureSource source = state::CaptureSource::Layer;
    bool tail = false;
    bool loop = false;

    // "4 bars · Layer", "4 bars +tail · Mix", "2 bars loop · Input"
    juce::String describe() const;

    bool operator== (const TrayEntry& other) const
    {
        return file == other.file && bars == other.bars && source == other.source && tail == other.tail &&
               loop == other.loop;
    }
};

class CaptureTray
{
public:
    static constexpr int kMaxEntries = 8;

    // Puts a new capture first; the oldest beyond 8 drops out. A file already
    // in the tray moves to the front.
    void add (const TrayEntry& entry);

    // Removes the entry for `file` (Remove from tray).
    void remove (const juce::File& file);

    // Drops entries whose file is gone. True when any did.
    bool dropMissing();

    const std::vector<TrayEntry>& getEntries() const noexcept { return entries; }
    int size() const noexcept { return static_cast<int> (entries.size()); }
    bool isEmpty() const noexcept { return entries.empty(); }

    // Invalid when the tray is empty.
    juce::ValueTree toValueTree() const;

    // Reads a tray; anything unreadable is skipped, at most 8 are kept.
    static CaptureTray fromValueTree (const juce::ValueTree& tree);

    bool operator== (const CaptureTray& other) const { return entries == other.entries; }

private:
    std::vector<TrayEntry> entries;
};

} // namespace thunder::capture
