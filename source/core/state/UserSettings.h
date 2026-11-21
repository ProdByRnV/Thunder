#pragma once

#include "core/capture/CaptureTypes.h"

#include <juce_core/juce_core.h>

namespace thunder::state
{

// Global settings shared by every Thunder instance and session (ARCHITECTURE
// §9.1), stored in `Thunder/settings.xml` in the user's app-data folder.
// Settings are added here as their features arrive.
//
// Message thread only. Each call reads or writes the file, so instances never
// work from a stale copy.
struct UserSettings
{
    juce::File capturesFolder;                                            // empty → the default
    capture::CaptureFormat captureFormat = capture::CaptureFormat::Int24; // PRD FR-CAPT-005
    bool preservePresetSession = true; // keep Key, Scale and Output Mode when loading presets (FR-PRESET-006)
    juce::String presetAuthor;         // remembered after the first save (UI.md §9.16)

    // `<user documents>/Thunder/Captures`.
    static juce::File defaultCapturesFolder();

    // The folder captures go to: the chosen one, or the default.
    juce::File getCapturesFolder() const;

    // `Thunder/settings.xml` in the user's app-data folder.
    static juce::File defaultFile();

    // Missing or unreadable settings give the defaults.
    static UserSettings load (const juce::File& file = defaultFile());

    // Returns false if the file couldn't be written.
    bool save (const juce::File& file = defaultFile()) const;

    bool operator== (const UserSettings&) const = default;
};

} // namespace thunder::state
