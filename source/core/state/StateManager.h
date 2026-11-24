#pragma once

#include "SessionState.h"

#include <juce_data_structures/juce_data_structures.h>

#include <optional>
#include <span>

// Saving and restoring Thunder's state, with versioning and migrations
// (PRD NFR-REL-002, §11.4; ARCHITECTURE §9).
//
// Saved state is one ValueTree:
//
//   <ThunderState stateVersion="2" pluginVersion="1.0.0">
//     <PARAMETERS> ... host parameters (APVTS) ... </PARAMETERS>
//     <Session presetName="" presetModified="0" captureBars="4" captureSource="layer" …>
//       <Source path="…" … />   (when the session has a source)
//     </Session>
//   </ThunderState>
//
// stateVersion goes up by one whenever the meaning of saved data changes, and
// each step gets a migration that turns version N into N + 1. Adding a
// parameter whose default reproduces the old sound needs no new version.

namespace thunder::state
{

// 1: before v1.0. 2: v1.0's parameters (StateManager.cpp says what changed).
inline constexpr int kCurrentStateVersion = 2;

// Turns a version-N state tree into version N + 1. Pure: no I/O, no globals.
using Migration = juce::ValueTree (*) (juce::ValueTree);

// Migrations in order: element i turns version i + 1 into version i + 2.
std::span<const Migration> stateMigrations() noexcept;

// [MSG/NRT] Builds the state tree for saving. `source` (the session's Source,
// source::SourceRef) and `morphSoundB` (Morph's sound B, perform::SoundB) go
// inside Session when valid.
juce::ValueTree createState (const juce::ValueTree& parameters, const SessionState& session,
                             const juce::String& pluginVersion, const juce::ValueTree& source = {},
                             const juce::ValueTree& morphSoundB = {});

struct LoadedState
{
    juce::ValueTree parameters; // type PARAMETERS, ready for APVTS::replaceState
    SessionState session;
    juce::ValueTree source;      // the session's Source child; invalid if there is none
    juce::ValueTree morphSoundB; // Morph's sound B (MorphB); invalid if there is none
    int savedVersion = 0;
    bool fromNewerVersion = false; // saved by a newer Thunder: known data loaded, the rest ignored
};

// [MSG/NRT] Reads and migrates a saved state tree. nullopt if it is not a
// Thunder state at all (the caller then keeps its current state).
std::optional<LoadedState> loadState (const juce::ValueTree& saved);

// Applies `migrations` to bring `tree` from `fromVersion` to `toVersion`. Works
// on a copy. Exposed so the migration loop can be tested with test migrations.
juce::ValueTree migrate (const juce::ValueTree& tree, int fromVersion, int toVersion,
                         std::span<const Migration> migrations);

} // namespace thunder::state
