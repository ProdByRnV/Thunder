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
inline const juce::Colour gridBar{0xff2a3348};
inline const juce::Colour gridBeat{0xff1a2030};

// Energy
inline const juce::Colour accentPrimary{0xff4fc3ff};
inline const juce::Colour accentPrimaryDim{0x594fc3ff}; // 35%
inline const juce::Colour accentSecondary{0xff8b5cff};
inline const juce::Colour accentSecondaryDim{0x598b5cff};
inline const juce::Colour accentHot{0xffe8f6ff};
inline const juce::Colour glowPrimary{0xff4fc3ff};

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

// The meters' empty track (§9.9).
inline const juce::Colour meterTrack{0xff0e1220};

// Visualiser (§3.6)
inline const juce::Colour waveDry{0xff3a4560};
inline const juce::Colour waveDryEdge{0xff55627f};
inline const juce::Colour waveLayer{0xff4fc3ff};
inline const juce::Colour waveLayerCore{0xffe8f6ff};
inline const juce::Colour spectrumDry{0xff55627f};
inline const juce::Colour spectrumLayer{0xff4fc3ff};
inline const juce::Colour filterCurve{0xffe6eaf2};

// Elevation (§6.1) and the window's texture (§6.4).
inline const juce::Colour shadow{0xff000000};
inline const juce::Colour highlight{0xffffffff}; // raised controls' inner top line, at opacity::topHighlight
inline const juce::Colour cloud{0xff0f1626};     // the soft blobs behind the visualiser well
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
inline constexpr float aboutWordmark = 22.0f; // the About panel's larger logo (§10.4)

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
inline constexpr float focusOffset = 2.0f; // the focus ring sits this far outside a control (§9)
} // namespace border

// ---- Elevation and glow (UI.md §6) ---------------------------------------------------------------------------------
struct Shadow
{
    int blur;
    int offsetY;
    float opacity;
};

namespace elevation
{
inline constexpr Shadow popover{24, 8, 0.50f}; // level 3: popovers, menus, tooltips
inline constexpr Shadow dialog{48, 16, 0.60f}; // level 4: dialogs
inline constexpr float wellShadow = 0.60f;     // the visualiser well's inner shadow (§6.2)
inline constexpr int wellShadowSize = 2;
} // namespace elevation

struct Glow
{
    float blur;    // logical px
    float opacity; // at full level
};

namespace glow
{
inline constexpr Glow subtle{6.0f, 0.25f};
inline constexpr Glow medium{12.0f, 0.40f};
inline constexpr Glow strong{24.0f, 0.55f};
inline constexpr Glow flash{40.0f, 0.70f};
inline constexpr float hoverBoost = 0.15f; // added to a knob's arc glow on hover (§9.1)
} // namespace glow

namespace texture
{
inline constexpr float grainOpacity = 0.025f; // film grain on the window background (§6.4)
inline constexpr float vignetteOpacity = 0.15f;
inline constexpr float cloudOpacity = 0.20f;
inline constexpr juce::uint32 grainSeed = 0x7d1a9e3bu; // the same grain every time
} // namespace texture

// ---- Layout (UI.md §7–§9) ------------------------------------------------------------------------------------------
namespace size
{
// Window (§7.1)
inline constexpr int baseWidth = 1040;
inline constexpr int baseHeight = 680;
inline constexpr float minScale = 0.75f;
inline constexpr float maxScale = 2.0f;
inline constexpr float scaleStep = 0.05f;                                              // corner-drag snaps to 5% steps
inline constexpr float scalePresets[] = {0.75f, 0.9f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f}; // Settings
inline constexpr int hostFrameWidth = 32;  // room left for a host's plugin-window frame when fitting the screen
inline constexpr int hostFrameHeight = 64; // (UI.md UQ-03)
inline constexpr int resizeHandle = 16;

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
inline constexpr float knobSpark = 4.0f;      // the dot at the arc's tip
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
inline constexpr float chainNotch = 5.0f; // the selected node's notch, pointing at the module panel
inline constexpr int controlHeight = 28;
inline constexpr int primaryButtonHeight = 32;
inline constexpr float pressedScale = 0.96f; // a pressed button (§9.6)
inline constexpr int segmentedSegment = 88;
inline constexpr int checkboxBox = 14;
inline constexpr int checkboxRowWidth = 180;
inline constexpr int iconButton = 28;
inline constexpr int icon = 16;
inline constexpr float iconStroke = 1.5f;
inline constexpr int presetBarWidth = 320;  // ◄ name ► ☆ Save ⋯ (narrowed for Morph, §9.17)
inline constexpr int tooltipMaxWidth = 260; // §9.14
inline constexpr int keyRootWidth = 56;
inline constexpr int keyScaleWidth = 148;
inline constexpr int keyBadgeWidth = 56; // the detected key: dot and confidence (§9.13)
inline constexpr int labelHeight = 16;
inline constexpr int valueHeight = 18;
inline constexpr int bolt = 18; // logo glyph height
inline constexpr int wordmarkWidth = 112;

// Morph in the top bar (§9.17): "A ──●── B"
inline constexpr int morphWidth = 128;
inline constexpr int morphEnd = 12; // the A and B labels
inline constexpr int morphThumb = 10;
inline constexpr float morphTrack = 2.0f;
// The PERFORM panel beside the visualiser (§9.17): 200 × 208.
inline constexpr int performWidth = 200;
inline constexpr int strikePadHeight = 36;
inline constexpr int riserHeight = 22;

// The capture tray (§9.18).
inline constexpr int trayButtonWidth = 40; // "+N"
inline constexpr int trayWidth = 440;
inline constexpr int trayDescribeWidth = 128; // "4 bars +tail · Mix"

// Knobs show the value Morph sends as a thin second ring (§9.17).
inline constexpr float knobMorphRing = 1.5f;
inline constexpr float knobMorphRingGap = 3.0f;
inline constexpr float menuSelectedBar = 2.0f;

// Output panel (§8.5 as laid out in the 4A mockup)
inline constexpr int outputKnobColumn = 64;
inline constexpr int meterBar = 8;
inline constexpr int meterGap = 4;
inline constexpr int correlationHeight = 4; // the correlation line under the meter (§8.5)
inline constexpr int limiterIcon = 10;      // the ⚠ after LIMITER
inline constexpr int loudnessWidth = 96;    // "−14.2 LUFS" in the Output header

// Visualiser (§8.2, §9.19, §11.6)
inline constexpr int viewToggleWidth = 136; // WAVE | SPECTRUM, bottom-right in the well
inline constexpr int viewToggleHeight = 22;
inline constexpr float waveStroke = 1.5f;
inline constexpr float spectrumStroke = 1.5f;
inline constexpr int filterHandle = 10;      // HP and LP handles
inline constexpr int filterHandleHit = 24;   // their hit area (§14)
inline constexpr int handleBubbleWidth = 72; // the value shown over a handle
inline constexpr int handleBubbleHeight = 20;
inline constexpr float grainSparkMin = 3.0f;
inline constexpr float grainSparkMax = 6.0f;
inline constexpr int toneCurveWidth = 380; // Tone's mini filter curve
inline constexpr int toneCurveHeight = 80;
inline constexpr int emptyBolt = 48;

// Module panels (§11.4, §11.8)
inline constexpr int chipWidth = 96; // Static's target chips
inline constexpr int chipGap = 8;
inline constexpr int shapeThumbWidth = 48; // Shape's picker: one thumbnail per shape
inline constexpr int shapeThumbHeight = 32;
inline constexpr float shapeThumbStroke = 1.5f;
inline constexpr int rateDropdownWidth = 120; // Shape's Rate

// The module panels' live displays (§11.3, §11.4, §11.8, §11.9)
inline constexpr int grainCloudWidth = 120;
inline constexpr int grainCloudHeight = 60;
inline constexpr int staticTraceWidth = 200;
inline constexpr int staticTraceHeight = 40;
inline constexpr int shapeDisplayWidth = 380;
inline constexpr int shapeDisplayHeight = 60;
inline constexpr int reductionMeterHeight = 28;
inline constexpr float reductionBarHeight = 8.0f;
inline constexpr int reductionValueWidth = 64;
inline constexpr int correlationMiniHeight = 10;
inline constexpr float displayStroke = 1.5f;
inline constexpr float displayDotMin = 1.5f;
inline constexpr float displayDotMax = 4.0f;

// Capture bar (§9.10)
inline constexpr int captureTitleWidth = 64;
inline constexpr int barsDropdownWidth = 112; // room for "4 bars +tail"
inline constexpr int sourceDropdownWidth = 104;
inline constexpr int armButtonWidth = 84;
inline constexpr int statusWidth = 240; // "LAT 10 ms · v1.0.0 · @ProdByRnV"
inline constexpr int dragThreshold = 4; // px before a press becomes a drag (§16.1)

// Overlays (§9.15–§10.4)
inline constexpr int dialogButtonWidth = 88;
inline constexpr int saveFormWidth = 420;
inline constexpr int saveFormLabelWidth = 96;
inline constexpr int saveFormDescriptionHeight = 64;
inline constexpr int toastWidth = 320;
inline constexpr int toastHeight = 44;
inline constexpr int toastActionsHeight = 76; // with a row of buttons (e.g. Locate… · Dismiss)
inline constexpr int toastButtonWidth = 84;
inline constexpr int maxTextLength = 128;
inline constexpr int maxDescriptionLength = 1024;
inline constexpr int settingsWidth = 380;
inline constexpr int settingsRowHeight = 36;
inline constexpr int settingsLabelWidth = 180;
inline constexpr int aboutWidth = 420;
inline constexpr int aboutHeight = 232;
inline constexpr int aboutCopyWidth = 120;   // "Copy details"
inline constexpr int aboutCreditWidth = 160; // "Made by @ProdByRnV"

// The preset browser (§9.12): a popover under the preset bar
inline constexpr int browserWidth = 640;
inline constexpr int browserHeight = 440;
inline constexpr int browserSidebarWidth = 148;
inline constexpr int browserRowHeight = 22;
inline constexpr int browserDetailHeight = 112;
inline constexpr int browserSearchWidth = 240;
inline constexpr int browserButtonWidth = 92;
inline constexpr int browserActionWidth = 84;
inline constexpr int browserColumnName = 152;
inline constexpr int browserColumnCategory = 84;
inline constexpr int browserColumnBank = 80;
inline constexpr int browserColumnAuthor = 96;
inline constexpr int browserColumnStar = 28;
inline constexpr int saveFormTagsHint = 160; // the Tags field's "comma, separated" hint width
inline constexpr int aboutBolt = 30;
} // namespace size

// ---- Opacity -------------------------------------------------------------------------------------------------------
namespace opacity
{
inline constexpr float disabledBody = 0.40f; // a switched-off module's controls (§8.4)
inline constexpr float placeholder = 0.18f;  // the bolt in the empty visualiser
inline constexpr float topHighlight = 0.04f; // raised controls' inner top highlight (§6.1)
inline constexpr float viewToggle = 0.50f;   // controls inside the visualiser well until hovered (§8.2)
} // namespace opacity

// ---- Motion and timing (UI.md §9, §13) -----------------------------------------------------------------------------
namespace motion
{
inline constexpr int fast = 80;    // button press, knob spark
inline constexpr int base = 150;   // hover states, toggle flash, chip appear
inline constexpr int medium = 220; // popovers, module switch
inline constexpr int slow = 320;
inline constexpr int fade = 250;
inline constexpr int frameHz = 60; // animation updates (UI-FX-004)
} // namespace motion

namespace timing
{
inline constexpr int uiRefreshHz = 20;
inline constexpr int tooltipDelayMs = 600;      // §9.14
inline constexpr int successHighlightMs = 3000; // capture chip after a capture (§9.10)
inline constexpr int toastMs = 4000;            // §9.15
} // namespace timing

// ---- Visualiser and meters (UI.md §8.2, §8.5, §9.9, §9.19) -------------------------------------------------------
namespace visualiser
{
// Source (§16.2): the strip at the top of the well, and the drop target.
inline constexpr int sourceStripHeight = 40;
inline constexpr int sourceNameWidth = 200;
inline constexpr int sourceKeyWidth = 120;
inline constexpr float sourceStripOpacity = 0.92f; // the strip's background over the waveform
inline constexpr float dropDash = 6.0f;            // the dashed outline while a file is dragged over
inline constexpr float dropGap = 4.0f;
inline constexpr float dropFill = 0.10f; // cyan wash over the well

// Waveform (§8.2)
inline constexpr int defaultBars = 2;          // time window; 1, 2 or 4 from the right-click menu
inline constexpr double maxSeconds = 32.0;     // the longest window kept (4 bars at 30 BPM)
inline constexpr float waveHeight = 0.45f;     // full scale reaches ±45% of the well's height
inline constexpr float layerFill = 0.55f;      // the layer's fill opacity
inline constexpr float layerCoreFrom = 0.90f;  // the top 10% of the layer's level turns hot
inline constexpr float dimmed = 0.50f;         // stopped transport
inline constexpr double dimAfterSeconds = 2.0; // … this long after it stopped
inline constexpr float silence = 0.001f;       // −60 dBFS: below this the well shows "Play your melody…"

// Spectrum (§8.2)
inline constexpr float minHz = 20.0f;
inline constexpr float maxHz = 20000.0f;
inline constexpr float minDb = -72.0f;
inline constexpr float maxDb = 0.0f;
inline constexpr float gridHz[] = {50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f};
inline constexpr float gridDb[] = {-12.0f, -24.0f, -36.0f, -48.0f, -60.0f};
inline constexpr float spectrumFill = 0.25f;
inline constexpr double smoothingOctaves = 1.0 / 6.0;
inline constexpr double releaseSeconds = 0.3;
inline constexpr float curveTopDb = -12.0f; // the filter curve's flat part sits on this line

// Meters (§9.9, §8.5)
inline constexpr float meterMinDb = -60.0f;
inline constexpr float meterHotDb = -3.0f; // the top 3 dB are drawn hot
inline constexpr float meterReleaseDbPerSecond = 20.0f;
inline constexpr double peakHoldSeconds = 1.5;
inline constexpr double clipHoldSeconds = 2.0;
inline constexpr double loudnessSeconds = 3.0; // short-term loudness (EBU R 128)
inline constexpr float loudnessFloor = -70.0f; // LUFS; quieter reads "–"
inline constexpr double correlationSeconds = 0.3;
inline constexpr float limiterWarnDb = 1.0f; // ⚠ when the limiter takes more than this
inline constexpr double limiterHoldSeconds = 0.5;

// Live grain view (§9.19)
inline constexpr double grainMaxSeconds = 4.0; // how far back a grain can sit
inline constexpr float grainSemitones = 12.0f; // the top of the well
inline constexpr float grainTrail = 0.25f;     // trail opacity
inline constexpr int reducedMotionHz = 10;     // static dots update rate under Reduce Motion
} // namespace visualiser

// ---- Lightning (UI.md §12; PRD UI-FX-001, UI-FX-002) ------------------------------------------------------------
namespace lightning
{
// Bolt geometry (§12.2)
inline constexpr int depthLong = 6;             // subdivision levels for a bolt longer than longBolt px
inline constexpr int depthShort = 5;            // … and for a shorter one
inline constexpr float longBolt = 150.0f;       // px
inline constexpr float displacement = 0.35f;    // × the segment length at the first level
inline constexpr float jitterDecay = 0.55f;     // per level
inline constexpr float branchChance = 0.12f;    // per subdivision of the main strand
inline constexpr float branchMinLength = 0.30f; // × the distance left to the end
inline constexpr float branchMaxLength = 0.60f;
inline constexpr int branchDepthDrop = 2; // a branch has this many fewer levels
inline constexpr float branchBrightness = 0.60f;
inline constexpr float branchMinAngle = 0.35f; // radians off the main direction (20°)
inline constexpr float branchMaxAngle = 0.80f; // (46°)
inline constexpr int maxBolts = 3;             // at the same time
inline constexpr float maxLean = 0.33f;        // a bolt starts up to this share of the width left of its end

// Rendering (§12.4)
inline constexpr float glowWidth = 10.0f;
inline constexpr float glowOpacity = 0.30f;
inline constexpr float bodyWidth = 3.0f;
inline constexpr float bodyOpacity = 0.85f;
inline constexpr float coreWidth = 1.0f;
inline constexpr float glowResolution = 0.25f; // the glow is blurred at this share of the display resolution

// Strike timeline (§12.4), seconds
inline constexpr double hold = 0.060;
inline constexpr double flickerGap = 0.030; // dark gap before the re-flicker
inline constexpr double flickerLength = 0.030;
inline constexpr float flickerLevel = 0.40f;
inline constexpr double fadeOut = 0.250;
inline constexpr double ambientFlash = 0.080; // the well brightens this long …
inline constexpr float ambientLevel = 0.06f;  // … by this much (+6% luminance)

// Triggers (§12.3)
inline constexpr float onsetShareStrong =
    0.80f; // a transient rises this share of the recent typical rise (sensitivity 0) …
inline constexpr float onsetShareSensitive = 0.40f; // … down to this at sensitivity 1 (Static Amount 100%) …
inline constexpr float onsetMinStrongDb = 6.0f;     // … and at least this far above the recent level (sensitivity 0) …
inline constexpr float onsetMinSensitiveDb = 3.0f;  // … down to this at sensitivity 1 …
inline constexpr float onsetMaxDb = 12.0f;          // … and never needs more than this
inline constexpr double riseMemory = 3.0;           // how long the typical rise is remembered (s)
inline constexpr float rearmShare = 0.5f;     // before the next strike the rise falls below this share of the threshold
inline constexpr double peakRelease = 0.020;  // the layer peak is held this long (s): one noisy frame doesn't count
inline constexpr float floorDb = -45.0f;      // nothing quieter strikes
inline constexpr double recentAttack = 0.120; // the recent level's time constants (s)
inline constexpr double recentRelease = 0.300;
inline constexpr double minStrikeInterval = 1.0 / 3.0; // never more than 3 strikes a second (§12.5)
inline constexpr double silenceSeconds = 2.0;          // after this long below floorDb, no bolts
inline constexpr double levelAttack = 0.010;           // bolt brightness follows the output level
inline constexpr double levelRelease = 0.250;
inline constexpr float dimLevelDb = -48.0f; // brightness: dimmest here …
inline constexpr float fullLevelDb = -6.0f; // … full from here
inline constexpr float dimBrightness = 0.35f;
} // namespace lightning

} // namespace thunder::ui::theme
