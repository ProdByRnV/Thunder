#pragma once

#include "core/state/UserSettings.h"

#include <juce_core/juce_core.h>

// Settings files for tests that open an editor.

namespace thunder::test
{

// Chooses Reduce Motion (off unless `reduce`) in `settingsFile`, so a test
// doesn't depend on the machine's animation setting, which the editor follows
// until it is chosen (on a CI runner with animations off it would be on).
inline void chooseMotion (const juce::File& settingsFile, bool reduce = false)
{
    auto settings = state::UserSettings::load (settingsFile);
    settings.reduceMotion = reduce;
    settings.reduceMotionChosen = true;
    settings.save (settingsFile);
}

} // namespace thunder::test
