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
    float uiScale = 0.0f;              // the editor size last chosen (UI.md §10.3); 0 = not set
    bool showTooltips = true;          // UI.md §10.3

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

    bool operator== (const UserSettings& other) const noexcept
    {
        return capturesFolder == other.capturesFolder && captureFormat == other.captureFormat &&
               preservePresetSession == other.preservePresetSession && presetAuthor == other.presetAuthor &&
               juce::exactlyEqual (uiScale, other.uiScale) && showTooltips == other.showTooltips;
    }
};

} // namespace thunder::state
