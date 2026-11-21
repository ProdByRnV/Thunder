#pragma once

#include <juce_graphics/juce_graphics.h>

// The UI's fonts (UI.md §4.1): `font.ui` / `font.display` for text and
// `font.mono` for numbers, so digits don't jump while a value changes.
//
// When the build embeds the font files (THUNDER_EMBEDDED_FONTS), these are
// Inter and JetBrains Mono; otherwise the system's sans-serif and monospace
// fonts stand in.

namespace thunder::ui::fonts
{

enum class Weight
{
    regular,
    medium,
    semiBold
};

juce::Font ui (float height, Weight weight = Weight::regular);
juce::Font mono (float height);

// True when Inter and JetBrains Mono are built in.
bool areEmbedded() noexcept;

} // namespace thunder::ui::fonts
