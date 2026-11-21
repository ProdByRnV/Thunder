#pragma once

#include "CaptureTypes.h"

#include <juce_core/juce_core.h>

// Capture file names (PRD FR-CAPT-009, ARCHITECTURE §8.3):
//   Thunder_<preset>_<key>_<bpm>bpm_<bars>bars_<yyyyMMdd-HHmmss>.wav
// e.g. Thunder_Night-Static_A-min_140bpm_4bars_20261121-143005.wav. The preset
// part is left out when no preset is loaded. Names are safe on every OS.

namespace thunder::capture
{

// "A-min", "C#-maj", "F-dorian", …
juce::String keyTag (int keyRoot, int keyScale);

// "140", or "92.5" for a fractional tempo.
juce::String bpmTag (double bpm);

// Keeps letters and digits; everything else becomes a single '-'. At most
// `maxLength` characters, no leading or trailing '-'.
juce::String sanitiseForFileName (const juce::String& text, int maxLength = 40);

// The file name without its extension.
juce::String buildCaptureName (const CaptureStart& start, const juce::String& presetName, juce::Time time);

// `<folder>/<name>.wav`, or `<name>_2.wav`, `<name>_3.wav`, … if a capture (or
// one still being written) already has the name.
juce::File uniqueCaptureFile (const juce::File& folder, const juce::String& name);

// The file a capture is written to until it is complete; never offered for drag-out.
juce::File partialFileFor (const juce::File& finalFile);

// A capture's name, claimed by creating its `.partial` file.
struct ClaimedCaptureFile
{
    juce::File finalFile;
    std::unique_ptr<juce::FileOutputStream> stream; // the open `.partial`; null if it couldn't be created
};

// [BG] Creates the folder if needed, picks a unique name (uniqueCaptureFile) and
// creates its `.partial`, in one step under a lock every Thunder instance in the
// process shares. Several instances armed together start on the same bar and
// second, so they ask for the same name (and maybe a new folder) at the same
// moment; each still gets its own file. The lock guards the shared folder, not
// any instance's state (PRD NFR-REL-004).
ClaimedCaptureFile claimCaptureFile (const juce::File& folder, const juce::String& name);

// [BG] Renames a finished `.partial` to `wanted`, or to the next free name if a
// capture from another process has taken it meanwhile; never replaces a file.
// Returns the file it became, or juce::File() if the rename failed.
juce::File finishCaptureFile (const juce::File& partial, const juce::File& wanted);

} // namespace thunder::capture
