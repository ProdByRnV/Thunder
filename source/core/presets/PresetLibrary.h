#pragma once

#include "PresetDocument.h"

#include <juce_core/juce_core.h>

#include <functional>
#include <optional>
#include <vector>

// Factory presets, the user preset library and safe saving (PRD FR-PRESET-001,
// 004, 005, 010–013; ARCHITECTURE §9.5). Everything here is plain functions;
// PresetManager runs the scan on its own thread.
//
// - Factory presets are built into the plugin (assets/presets/, embedded at
//   build time) and are read-only. Each lists only the settings it changes.
// - Init is the empty preset: every parameter at its default, so it can never
//   drift from what a fresh instance sounds like.
// - User presets live in `<user documents>/Thunder/Presets`, created on the
//   first save. Sub-folders are banks ("Trap", "Trap/Dark").
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

// The name every factory preset's bank shows.
inline constexpr const char* kFactoryBank = "Factory";

// One preset in the library, with what the browser shows and searches.
struct Entry
{
    juce::String name;
    juce::String author;
    juce::String category;
    juce::String tags; // comma-separated, as stored
    juce::String description;
    juce::String bank; // user presets: the folder below the library ("" = top level, "Trap/Dark")
    juce::String key;
    juce::File file; // user presets only
    bool isFactory = false;

    // The tags one by one, trimmed, without empty ones.
    juce::StringArray tagList() const;

    // What the bank column shows: "Factory", "User" for the top level, else the bank.
    juce::String bankLabel() const;

    // Identifies the bank for filters: "factory", "user:" + bank.
    juce::String bankId() const;
};

struct Listing
{
    std::vector<Entry> entries; // factory presets by name, then the user library by bank and name
    int unreadableFiles = 0;    // counted and reported, never shown by name
    bool incomplete = false;    // the scan hit a limit (presets or folder levels)
    bool userFolderMissing = false;

    int numUserPresets() const noexcept;
};

// The library's limits (PRD FR-PRESET-012). The folder-level limit also ends
// symbolic-link loops.
inline constexpr int kMaxLibraryPresets = 10000;
inline constexpr int kMaxBankDepth = 8;

// `<user documents>/Thunder/Presets`.
juce::File defaultUserFolder();

// The factory presets plus every preset in `userFolder` and its banks, each
// read through the validated reader (a file the scan lists is a file that
// loads). `shouldStop` is polled between files: a cancelled scan returns what
// it had, marked incomplete. `maxPresets` and `maxDepth` exist for tests.
Listing scanLibrary (const juce::File& userFolder, const std::function<bool()>& shouldStop = {},
                     int maxPresets = kMaxLibraryPresets, int maxDepth = kMaxBankDepth);

// The file a preset called `name` is saved as in `folder`: path separators and
// characters no OS allows are removed, leading and trailing spaces and dots are
// trimmed, and Windows' reserved names are refused. nullopt when nothing usable
// is left or the result would be outside the folder. This is the only function
// that turns a name into a file, so "would this replace a preset?" and the save
// itself can never disagree.
std::optional<juce::File> presetFileFor (const juce::File& folder, const juce::String& name);

// The folder of bank `bank` ("" = `userFolder` itself; "Trap/Dark" nests), each
// level cleaned like a preset name. nullopt if a level is unusable, there are
// more than kMaxBankDepth levels, or the result is outside the library.
std::optional<juce::File> bankFolderFor (const juce::File& userFolder, const juce::String& bank);

enum class SaveResult
{
    ok,
    invalidName,
    outsideLibrary,
    isFactoryPreset,
    couldNotCreateFolder,
    couldNotWrite,
    nameTaken // a rename or import would replace another preset
};

// Writes `preset` to `file` through a temporary file beside it, moved into
// place only when complete: a failed save leaves an existing preset intact.
SaveResult writePresetFile (const juce::File& file, const Preset& preset, const juce::String& thunderVersion);

// What to show the user for a refused save (UI.md §9.16).
juce::String describe (SaveResult result);

// Renames the user preset in `file` (its name inside and its file name, in the
// same bank) without ever replacing another file. On success `renamed` is the
// new file and the old one is gone.
SaveResult renamePresetFile (const juce::File& file, const juce::String& newName, const juce::String& thunderVersion,
                             juce::File& renamed);

// Two presets are the same document when their details and every value match.
bool sameDocument (const Preset& a, const Preset& b);

// Importing (PRD FR-PRESET-013): each file is read exactly as a scan would; a
// preset already in the library is left alone; a different preset with a name
// that is taken is saved as "Name (2)", "Name (3)"…; nothing is ever replaced.
struct ImportReport
{
    int imported = 0;
    int alreadyInLibrary = 0;
    int notPresets = 0; // not a Thunder preset, damaged or unreadable
    int couldNotSave = 0;
    juce::Array<juce::File> written;

    // "Imported 3 presets · 1 already in your library · 1 isn't a Thunder preset".
    juce::String describe() const;
};

ImportReport importPresets (const juce::Array<juce::File>& files, const juce::File& userFolder,
                            const juce::String& thunderVersion);

} // namespace thunder::presets
