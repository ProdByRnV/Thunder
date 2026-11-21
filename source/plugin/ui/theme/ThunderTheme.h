#pragma once

#include <juce_graphics/juce_graphics.h>

// Thunder's design tokens (UI.md §3–§6). Every colour, size and timing in the
// UI comes from here; nothing is hard-coded in components (UI.md §0.1).
//
// TODO(PRD): only the tokens the capture bar uses so far. The full token set,
// fonts and ThunderLookAndFeel arrive with the custom UI (ROADMAP T-0.5-05).

namespace thunder::ui::theme
{

namespace colour
{
inline const juce::Colour bgPanel{0xff141824};
inline const juce::Colour borderSubtle{0xff232a3b};
inline const juce::Colour textPrimary{0xffe6eaf2};
inline const juce::Colour textSecondary{0xff8a93a8};
inline const juce::Colour accentPrimary{0xff4fc3ff};
inline const juce::Colour accentPrimaryDim{0x594fc3ff}; // 35%
inline const juce::Colour stateError{0xffff5c7a};
inline const juce::Colour stateSuccess{0xff5cffb5};
inline const juce::Colour stateRecording{0xffff5c7a};
} // namespace colour

namespace space
{
inline constexpr int s1 = 4;
inline constexpr int s2 = 8;
inline constexpr int s3 = 12;
} // namespace space

namespace radius
{
inline constexpr float sm = 4.0f;
} // namespace radius

namespace size
{
inline constexpr int bottomBarHeight = 52; // R-BOTTOMBAR (UI.md §7)
inline constexpr int captureTitleWidth = 64;
inline constexpr int controlHeight = 28;
inline constexpr int barsDropdownWidth = 96;
inline constexpr int sourceDropdownWidth = 104;
inline constexpr int armButtonWidth = 84;
inline constexpr int iconButtonWidth = 72;
inline constexpr int menuButtonWidth = 28;
inline constexpr int minEditorWidth = 760;
inline constexpr int paramListHeight = 400;
inline constexpr int dragThreshold = 4; // px before a press becomes a drag (UI.md §16.1)
} // namespace size

namespace type
{
inline constexpr float label = 10.5f;
inline constexpr float body = 13.0f;
} // namespace type

namespace timing
{
inline constexpr int uiRefreshHz = 20;
inline constexpr int tooltipDelayMs = 600;      // UI.md §9.14
inline constexpr int successHighlightMs = 3000; // chip border after a capture (UI.md §9.10)
} // namespace timing

} // namespace thunder::ui::theme
