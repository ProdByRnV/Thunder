#pragma once

namespace thunder::state
{
struct UserSettings;
}

// Reduce Motion (PRD UI-FX-005; UI.md §12.5, P5): when on, nothing animates
// for decoration (no lightning, flashes, sparks or playheads); what carries
// information (waveform, spectrum, meters, values) keeps updating. The setting
// is global, so one switch for the process: every editor applies it when it
// opens and when it changes, and components read it while drawing.
// Message thread only.

namespace thunder::ui::reduceMotion
{

bool isOn() noexcept;
void setOn (bool on) noexcept;

// The operating system's "reduce animations" preference, where Thunder can
// read it (Windows: "Animation effects" off); false elsewhere.
bool systemPrefers();

// What the settings ask for: the user's choice, or the system's preference
// until they have chosen.
bool wanted (const state::UserSettings& settings);

} // namespace thunder::ui::reduceMotion
