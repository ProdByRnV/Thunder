#pragma once

#include "core/presets/PresetFilter.h"
#include "core/presets/PresetLibrary.h"
#include "core/state/SessionState.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <functional>
#include <memory>

namespace thunder
{

// Loads and saves presets on the live parameters, and keeps the library
// (PRD FR-PRESET-004…007, 009…015; ARCHITECTURE §9.5). Message thread, except
// isModified() and rememberLoadedSound(), which a host may call from its state
// thread.
//
// - Loading writes each parameter through the host (with a change gesture), so
//   the host sees it like automation and the engine's smoothing and crossfades
//   make it click-free. A refused file changes nothing.
// - With "preserve session" on (the default), Key, Scale and Output Mode are
//   left as they are (FR-PRESET-006).
// - "Modified" means the sound differs from the loaded preset; it is stored in
//   the session with the preset's name and key.
// - The library is scanned on a thread of its own (FR-PRESET-012); until the
//   first scan finishes the listing holds the factory presets.
// - The browser's search, filters and sort live here, so ◄ ► step through the
//   list as it is filtered even while the browser is closed.
class PresetManager
{
public:
    // How the manager reads and writes the session it lives in.
    struct SessionAccess
    {
        std::function<state::SessionState()> get;
        std::function<void (const state::SessionState&)> set;
    };

    PresetManager (juce::AudioProcessorValueTreeState& apvts, SessionAccess session, juce::File settingsFile);
    ~PresetManager();

    // Where user presets are saved; tests point it elsewhere. Starts a new scan
    // if one was ever asked for.
    void setUserFolder (const juce::File& folder);
    const juce::File& getUserFolder() const noexcept { return userFolder; }
    void setSettingsFile (const juce::File& file)
    {
        settingsFile = file;
        favourites.reset();
    }

    // ---- The library ----------------------------------------------------------------------------------------------

    // The latest finished scan (the first call starts one).
    const presets::Listing& getListing();

    // [MSG] Scans the folder again in the background; a newer scan cancels an older one.
    void rescan();

    // [MSG] Scans now, on this thread (tests; small libraries).
    void rescanNow();

    bool isScanning() const;

    // [MSG] Takes a finished scan. True when the listing changed (the UI polls it).
    bool collectScan();

    // [MSG] For tests: waits until no scan is running, then collects it.
    bool waitForScan (int timeoutMs = 5000);

    // Goes up each time the listing changes.
    int getListingVersion() const noexcept { return listingVersion; }

    // The browser's filter and sort (not saved; per instance).
    const presets::Filter& getFilter() const noexcept { return filter; }
    void setFilter (const presets::Filter& f) { filter = f; }
    const presets::Sort& getSort() const noexcept { return sort; }
    void setSort (const presets::Sort& s) { sort = s; }

    // The presets as filtered and sorted: what the browser lists and ◄ ► step through.
    std::vector<presets::Entry> getFilteredEntries();

    // The entry with this key in the listing, if it is there.
    std::optional<presets::Entry> findEntry (const juce::String& key);

    // Favourites (global, by key).
    juce::StringArray getFavourites() const;
    bool isFavourite (const juce::String& key) const;
    void setFavourite (const juce::String& key, bool favourite);

    // ---- The loaded sound -----------------------------------------------------------------------------------------

    struct Current
    {
        juce::String name;
        juce::String key;
        bool modified = false;
        bool isFactory() const { return key.startsWith ("factory:"); }
        bool isInit() const { return key.isEmpty() || key == presets::kInitKey; }
        bool isUser() const { return key.startsWith ("user:"); }
    };

    Current getCurrent() const;
    const presets::Metadata& getLoadedMetadata() const noexcept { return loadedMeta; }

    // The bank of the loaded user preset ("" for the top level or anything else).
    juce::String getLoadedBank() const;

    // Each returns an empty string on success, or a message for the user.
    juce::String load (const presets::Entry& entry);
    juce::String loadInit();
    juce::String loadFile (const juce::File& file);

    // ◄ / ► through the filtered list, wrapping at the ends. From a sound that
    // isn't in the list, ► goes to the first and ◄ to the last.
    juce::String step (int direction);

    // Reads a preset without loading it (Morph's "Load B from preset").
    std::optional<presets::Preset> readEntry (const presets::Entry& entry) const;

    // Called after a preset loads or saves, with its key.
    std::function<void (const juce::String& key)> onLoaded;

    // ---- Undoing a preview (the browser's Esc) --------------------------------------------------------------------

    struct RevertPoint
    {
        std::array<float, params::kNumParams> values{};
        juce::String presetName, presetKey;
        bool modified = false;
        presets::Metadata meta;
    };

    RevertPoint makeRevertPoint() const;
    void revertTo (const RevertPoint& point);

    // ---- Saving and managing --------------------------------------------------------------------------------------

    // The file a Save as with this name (in this bank) would write, and whether it exists.
    std::optional<juce::File> fileForName (const juce::String& name, const juce::String& bank = {}) const;
    bool wouldReplace (const juce::String& name, const juce::String& bank = {}) const;

    // Saves the current sound as a user preset in `bank` (replacing a file with
    // that name: the UI asks first) and makes it the loaded preset.
    presets::SaveResult saveAs (const presets::Metadata& meta, const juce::String& bank = {});

    // Saves over the loaded user preset. Factory presets and Init are refused.
    presets::SaveResult save();

    // User presets only: rename (never replacing another file) and delete.
    presets::SaveResult rename (const presets::Entry& entry, const juce::String& newName);
    bool remove (const presets::Entry& entry);

    // Import (never replaces anything) and export (the current sound with the
    // loaded preset's details, anywhere).
    presets::ImportReport importFiles (const juce::Array<juce::File>& files);
    presets::SaveResult exportTo (const juce::File& file);

    // Creates the user folder if needed and shows it in the file manager.
    bool openFolder();

    // ---- Host programs (FR-PRESET-015) ----------------------------------------------------------------------------

    // Init, then the factory presets by name.
    static int numPrograms();
    static juce::String programName (int index);
    static juce::String programKey (int index);
    static int programForKey (const juce::String& key); // -1 when the key isn't a program
    juce::String loadProgram (int index);

    // ---- Settings -------------------------------------------------------------------------------------------------

    bool getPreserveSession() const;
    void setPreserveSession (bool preserve);
    juce::String getRememberedAuthor() const;

    // [any thread] True when the sound differs from the loaded preset.
    bool isModified() const;

    // [any thread] After a session was restored: the sound now playing is the
    // loaded preset unless the session says it was modified.
    void rememberLoadedSound (const state::SessionState& restored);

private:
    using Values = std::array<float, params::kNumParams>;
    class Scanner;

    Values currentValues() const;
    void apply (const presets::Preset& preset);
    void becomeLoaded (const presets::Metadata& meta, const juce::String& key);
    void setListing (presets::Listing newListing);
    void libraryChanged(); // after a save, rename, delete or import

    juce::AudioProcessorValueTreeState& apvts;
    SessionAccess session;
    juce::File settingsFile;
    juce::File userFolder;

    std::unique_ptr<Scanner> scanner;
    presets::Listing listing;
    bool scanRequested = false;
    int listingVersion = 0;
    presets::Filter filter;
    presets::Sort sort;
    mutable std::optional<juce::StringArray> favourites; // the settings file's, read when needed
    presets::Metadata loadedMeta;

    mutable juce::CriticalSection snapshotLock; // never taken on the audio thread
    Values snapshot{};
    bool snapshotValid = true; // false: "modified" stays until the next load or save

    JUCE_DECLARE_NON_COPYABLE (PresetManager)
};

} // namespace thunder
