#pragma once

#include "PresetDocument.h"

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

// Factory presets, the user preset folder and safe saving (PRD FR-PRESET-001,
// 005, 010, 011; ARCHITECTURE §9.5). Message thread only.
//
// - Factory presets are built into the plugin (assets/presets/, embedded at
//   build time) and are read-only. Each lists only the settings it changes.
// - Init is the empty preset: every parameter at its default, so it can never
//   drift from what a fresh instance sounds like.
// - User presets live in `<user documents>/Thunder/Presets`, created on the
//   first save. v0.5 lists its top level; banks (sub-folders) arrive with the
//   full library.
// - A preset is identified by a key stored in the session: `factory:<name>`,
//   `user:<full path>` or `init` (a user file named "Init" is not the built-in Init).

namespace thunder::presets
{

struct FactoryPreset
{
    juce::String resourceName; // the embedded file
    Preset preset;
    ReadReport report;
};

// Every factory preset, read once through the normal reader and sorted by name.
const std::vector<FactoryPreset>& factoryPresets();

// The empty preset, named "Init".
Preset initPreset();

inline constexpr const char* kInitKey = "init";
juce::String factoryKey (const juce::String& name);
juce::String userKey (const juce::File& file);

// One preset in the ◄ ► order: factory presets first, then user presets, each by name.
struct Entry
{
    juce::String name;
    juce::String category;
    juce::String key;
    juce::File file; // user presets only
    bool isFactory = false;
};

struct Listing
{
    std::vector<Entry> entries;
    int unreadableFiles = 0; // counted and reported, never shown by name
};

// `<user documents>/Thunder/Presets`.
juce::File defaultUserFolder();

// The factory presets plus the top level of `userFolder` (which may not exist).
Listing listPresets (const juce::File& userFolder);

// The file a preset called `name` is saved as in `folder`: path separators and
// characters no OS allows are removed, leading and trailing spaces and dots are
// trimmed, and Windows' reserved names are refused. nullopt when nothing usable
// is left or the result would be outside the folder. This is the only function
// that turns a name into a file, so "would this replace a preset?" and the save
// itself can never disagree.
std::optional<juce::File> presetFileFor (const juce::File& folder, const juce::String& name);

enum class SaveResult
{
    ok,
    invalidName,
    outsideLibrary,
    isFactoryPreset,
    couldNotCreateFolder,
    couldNotWrite
};

// Writes `preset` to `file` through a temporary file beside it, moved into
// place only when complete: a failed save leaves an existing preset intact.
SaveResult writePresetFile (const juce::File& file, const Preset& preset, const juce::String& thunderVersion);

// What to show the user for a refused save (UI.md §9.16).
juce::String describe (SaveResult result);

} // namespace thunder::presets
