#pragma once

#include <juce_graphics/juce_graphics.h>

// Thunder's design tokens (UI.md §3–§6, §13). Every colour, size, font size,
// spacing, radius and timing in the UI comes from here; nothing is hard-coded
// in components (UI.md §0.1, PRD UI-GEN-003). Sizes are logical pixels at the
// base size (1040 × 680); the editor scales everything uniformly.

namespace thunder::ui::theme
{

// ---- Colours (UI.md §3) --------------------------------------------------------------------------------------------
namespace colour
{
// Surfaces
inline const juce::Colour bgDeep{0xff06070c};
inline const juce::Colour bgBase{0xff0a0c14};
inline const juce::Colour bgPanel{0xff141824};
inline const juce::Colour bgRaised{0xff1b2131};
inline const juce::Colour bgHover{0xff222a3d};
inline const juce::Colour bgOverlay{0xcc06070c};

// Lines
inline const juce::Colour borderSubtle{0xff232a3b};
inline const juce::Colour borderStrong{0xff33405a};

// Energy
inline const juce::Colour accentPrimary{0xff4fc3ff};
inline const juce::Colour accentPrimaryDim{0x594fc3ff}; // 35%
inline const juce::Colour accentSecondary{0xff8b5cff};
inline const juce::Colour accentHot{0xffe8f6ff};

// Text
inline const juce::Colour textPrimary{0xffe6eaf2};
inline const juce::Colour textSecondary{0xff8a93a8};
inline const juce::Colour textDisabled{0xff4a5368};
inline const juce::Colour textOnAccent{0xff06070c};

// Status
inline const juce::Colour stateWarning{0xffffc94d};
inline const juce::Colour stateError{0xffff5c7a};
inline const juce::Colour stateSuccess{0xff5cffb5};
inline const juce::Colour stateRecording{0xffff5c7a};
} // namespace colour

// ---- Typography (UI.md §4) -----------------------------------------------------------------------------------------
namespace type
{
inline constexpr float wordmark = 18.0f;
inline constexpr float title = 13.0f;
inline constexpr float body = 13.0f;
inline constexpr float label = 10.5f;
inline constexpr float value = 12.0f;
inline constexpr float valueLarge = 16.0f;
inline constexpr float caption = 10.0f;

// Letter spacing, as a fraction of the font height.
inline constexpr float wordmarkTracking = 0.28f;
inline constexpr float titleTracking = 0.12f;
inline constexpr float labelTracking = 0.08f;
} // namespace type

// ---- Spacing, radius, borders (UI.md §5) ---------------------------------------------------------------------------
namespace space
{
inline constexpr int s1 = 4;
inline constexpr int s2 = 8;
inline constexpr int s3 = 12;
inline constexpr int s4 = 16;
inline constexpr int s5 = 24;
inline constexpr int s6 = 32;
inline constexpr int s7 = 48;
} // namespace space

namespace radius
{
inline constexpr float sm = 4.0f;
inline constexpr float md = 8.0f;
inline constexpr float lg = 12.0f;
inline constexpr float pill = 999.0f;
} // namespace radius

namespace border
{
inline constexpr float width = 1.0f;
} // namespace border

// ---- Layout (UI.md §7–§9) ------------------------------------------------------------------------------------------
namespace size
{
// Window (§7.1)
inline constexpr int baseWidth = 1040;
inline constexpr int baseHeight = 680;
inline constexpr float minScale = 0.75f;
inline constexpr float maxScale = 2.0f;

// Regions (§7.2)
inline constexpr int topBarHeight = 52;
inline constexpr int visualiserHeight = 232;
inline constexpr int chainHeight = 64;
inline constexpr int outputWidth = 212;
inline constexpr int bottomBarHeight = 52;
inline constexpr int moduleHeaderHeight = 40;
inline constexpr int moduleColumnWidth = 180;
inline constexpr int moduleGutter = 16;

// Components (§9)
inline constexpr int knobLarge = 72;
inline constexpr int knobMedium = 56;
inline constexpr int knobSmall = 40;
inline constexpr float knobTrack = 3.0f;
inline constexpr float knobPointer = 2.0f;
inline constexpr float knobFace = 0.70f;        // of the diameter
inline constexpr float knobPointerFrom = 0.30f; // of the radius
inline constexpr float knobPointerTo = 0.46f;
inline constexpr float knobArcStart = -0.75f; // × π from 12 o'clock: 225° (UI.md §9.1)
inline constexpr float knobArcEnd = 0.75f;    // × π: −45°
inline constexpr int knobDragPixels = 200;    // full range
inline constexpr float knobFineFactor = 0.1f; // Shift-drag
inline constexpr float knobWheelStep = 0.01f; // 1% per wheel notch
inline constexpr int toggleWidth = 32;
inline constexpr int toggleHeight = 18;
inline constexpr int toggleKnob = 14;
inline constexpr int chainNodeWidth = 84;
inline constexpr int chainNodeHeight = 28;
inline constexpr int chainDot = 8;
inline constexpr float chainWire = 2.0f;
inline constexpr int controlHeight = 28;
inline constexpr int segmentedSegment = 88;
inline constexpr int checkboxBox = 14;
inline constexpr int checkboxRowWidth = 180;
inline constexpr int iconButton = 28;
inline constexpr int presetBarWidth = 340;
inline constexpr int tooltipMaxWidth = 260; // §9.14
inline constexpr int keyRootWidth = 64;
inline constexpr int keyScaleWidth = 148;
inline constexpr int labelHeight = 16;
inline constexpr int valueHeight = 18;
inline constexpr int bolt = 18; // logo glyph height
inline constexpr int wordmarkWidth = 140;
inline constexpr int placeholderBolt = 64; // visualiser placeholder glyph

// Capture bar (§9.10)
inline constexpr int captureTitleWidth = 64;
inline constexpr int barsDropdownWidth = 96;
inline constexpr int sourceDropdownWidth = 104;
inline constexpr int armButtonWidth = 84;
inline constexpr int iconButtonWidth = 72;
inline constexpr int menuButtonWidth = 28;
inline constexpr int statusWidth = 150;
inline constexpr int dragThreshold = 4; // px before a press becomes a drag (§16.1)

// Overlays (§9.15–§9.16)
inline constexpr int saveFormWidth = 420;
inline constexpr int saveFormLabelWidth = 96;
inline constexpr int saveFormDescriptionHeight = 64;
inline constexpr int toastWidth = 320;
inline constexpr int toastHeight = 44;
inline constexpr int maxTextLength = 128;
inline constexpr int maxDescriptionLength = 1024;
} // namespace size

// ---- Opacity -------------------------------------------------------------------------------------------------------
namespace opacity
{
inline constexpr float disabledBody = 0.40f; // a switched-off module's controls (§8.4)
inline constexpr float placeholder = 0.18f;  // the visualiser placeholder glyph
inline constexpr float topHighlight = 0.04f; // raised controls' inner top highlight (§6.1)
} // namespace opacity

// ---- Timing (UI.md §9, §13) ----------------------------------------------------------------------------------------
namespace timing
{
inline constexpr int uiRefreshHz = 20;
inline constexpr int tooltipDelayMs = 600;      // §9.14
inline constexpr int successHighlightMs = 3000; // capture chip after a capture (§9.10)
inline constexpr int toastMs = 4000;            // §9.15
} // namespace timing

} // namespace thunder::ui::theme
