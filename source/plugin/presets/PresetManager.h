#pragma once

#include "core/presets/PresetLibrary.h"
#include "core/state/SessionState.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <functional>

namespace thunder
{

// Loads and saves presets on the live parameters (PRD FR-PRESET-005…007, 009…011,
// 014; ARCHITECTURE §9.5). Message thread, except isModified() and
// rememberLoadedSound(), which a host may call from its state thread.
//
// - Loading writes each parameter through the host (with a change gesture), so
//   the host sees it like automation and the engine's smoothing and crossfades
//   make it click-free. A refused file changes nothing.
// - With "preserve session" on (the default), Key, Scale and Output Mode are
//   left as they are (FR-PRESET-006).
// - "Modified" means the sound differs from the loaded preset; it is stored in
//   the session with the preset's name and key.
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

    // Where user presets are saved; tests point it elsewhere.
    void setUserFolder (const juce::File& folder);
    const juce::File& getUserFolder() const noexcept { return userFolder; }
    void setSettingsFile (const juce::File& file) { settingsFile = file; }

    // Factory presets, then user presets. Re-read by refresh().
    const presets::Listing& getListing();
    void refresh();

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

    // Each returns an empty string on success, or a message for the user.
    juce::String load (const presets::Entry& entry);
    juce::String loadInit();
    juce::String loadFile (const juce::File& file);

    // ◄ / ► through the listing, wrapping at the ends. From a sound that isn't
    // in the list, ► goes to the first and ◄ to the last.
    juce::String step (int direction);

    // The file a Save as with this name would write, and whether it exists.
    std::optional<juce::File> fileForName (const juce::String& name) const;
    bool wouldReplace (const juce::String& name) const;

    // Saves the current sound as a user preset (replacing a file with that name:
    // the UI asks first) and makes it the loaded preset.
    presets::SaveResult saveAs (const presets::Metadata& meta);

    // Saves over the loaded user preset. Factory presets and Init are refused.
    presets::SaveResult save();

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

    Values currentValues() const;
    void apply (const presets::Preset& preset);
    void becomeLoaded (const presets::Metadata& meta, const juce::String& key);

    juce::AudioProcessorValueTreeState& apvts;
    SessionAccess session;
    juce::File settingsFile;
    juce::File userFolder;

    presets::Listing listing;
    bool listingValid = false;
    presets::Metadata loadedMeta;

    mutable juce::CriticalSection snapshotLock; // never taken on the audio thread
    Values snapshot{};
    bool snapshotValid = true; // false: "modified" stays until the next load or save

    JUCE_DECLARE_NON_COPYABLE (PresetManager)
};

} // namespace thunder
