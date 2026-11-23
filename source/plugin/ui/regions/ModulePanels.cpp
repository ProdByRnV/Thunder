#include "ModulePanels.h"

#include "core/dsp/shape/ShapeModule.h"
#include "core/dsp/space/SpaceModule.h"
#include "plugin/ui/Motion.h"
#include "plugin/ui/theme/ThunderFonts.h"
#include "plugin/ui/theme/ThunderIcons.h"
#include "plugin/ui/theme/ThunderTheme.h"

namespace thunder::ui
{

using namespace theme;
using params::ParamId;

namespace
{
juce::Rectangle<int> knobIn (juce::Rectangle<int> cellArea, ThunderKnob::Size s)
{
    const auto preferred = ThunderKnob::preferredBounds (s);
    return cellArea.withSizeKeepingCentre (std::min (cellArea.getWidth(), preferred.getWidth()), preferred.getHeight())
        .withY (cellArea.getY());
}

int knobHeight (ThunderKnob::Size s)
{
    return ThunderKnob::preferredBounds (s).getHeight();
}

// A caption over a control that carries its own name (a dropdown, a display).
void setUpCaption (juce::Label& caption, const juce::String& text)
{
    caption.setText (text, juce::dontSendNotification);
    caption.setFont (fonts::ui (type::label, fonts::Weight::medium));
    caption.setColour (juce::Label::textColourId, colour::textSecondary);
    caption.setJustificationType (juce::Justification::centred);
    caption.setInterceptsMouseClicks (false, false);
    caption.setAccessible (false);
}
} // namespace

// ---- ModulePanel
// -------------------------------------------------------------------------------------------------------

ModulePanel::ModulePanel (juce::AudioProcessorValueTreeState& state, const juce::String& titleText, ParamId onParam)
    : apvts (state)
    , title (titleText)
    , toggle (state, onParam)
    , resetButton ("Reset " + titleText, icons::reset())
    , moduleOnParam (onParam)
    , onWatcher (state, onParam,
                 [this] (float on)
                 {
                     body.setAlpha (on >= 0.5f ? 1.0f : opacity::disabledBody);
                     repaint();
                 })
{
    setTitle (titleText);
    resetButton.setTooltip ("Reset " + titleText + ": every " + titleText + " control back to its default");
    resetButton.onClick = [this] { resetToDefaults(); };
    addAndMakeVisible (resetButton);
    addAndMakeVisible (toggle);
    addAndMakeVisible (body);
}

ModulePanel::~ModulePanel() = default;

void ModulePanel::resetToDefaults()
{
    const auto group = params::paramDef (moduleOnParam).group;
    for (const auto& def : params::kParamDefs)
    {
        if (def.group != group || def.param == moduleOnParam)
            continue;

        auto& p = parameter (apvts, def.param);
        p.beginChangeGesture();
        p.setValueNotifyingHost (p.getDefaultValue());
        p.endChangeGesture();
    }
}

ThunderKnob& ModulePanel::addKnob (ParamId id, const juce::String& label, ThunderKnob::Size s)
{
    auto knob = std::make_unique<ThunderKnob> (apvts, id, label, s);
    auto& ref = *knob;
    body.addAndMakeVisible (ref);
    controls.push_back (std::move (knob));
    return ref;
}

SegmentedControl& ModulePanel::addSegmented (ParamId id, std::vector<int> options, juce::StringArray labels)
{
    auto control = std::make_unique<SegmentedControl> (apvts, id, std::move (options), std::move (labels));
    auto& ref = *control;
    body.addAndMakeVisible (ref);
    controls.push_back (std::move (control));
    return ref;
}

ParamCheckbox& ModulePanel::addCheckbox (ParamId id, const juce::String& label)
{
    auto box = std::make_unique<ParamCheckbox> (apvts, id, label);
    auto& ref = *box;
    body.addAndMakeVisible (ref);
    controls.push_back (std::move (box));
    return ref;
}

ParamChip& ModulePanel::addChip (ParamId id, const juce::String& label, juce::Colour accent)
{
    auto chip = std::make_unique<ParamChip> (apvts, id, label, accent);
    auto& ref = *chip;
    body.addAndMakeVisible (ref);
    controls.push_back (std::move (chip));
    return ref;
}

ParamDropdown& ModulePanel::addDropdown (ParamId id)
{
    auto dropdown = std::make_unique<ParamDropdown> (apvts, id);
    auto& ref = *dropdown;
    body.addAndMakeVisible (ref);
    controls.push_back (std::move (dropdown));
    return ref;
}

juce::Rectangle<int> ModulePanel::cell (juce::Rectangle<int> row, int column, int span)
{
    const auto x = row.getX() + column * (size::moduleColumnWidth + size::moduleGutter);
    const auto w = span * size::moduleColumnWidth + (span - 1) * size::moduleGutter;
    return {x, row.getY(), w, row.getHeight()};
}

void ModulePanel::paint (juce::Graphics& g)
{
    g.fillAll (colour::bgPanel);

    auto header = getLocalBounds().removeFromTop (size::moduleHeaderHeight).reduced (space::s4, 0);
    g.setColour (colour::textPrimary);
    g.setFont (fonts::ui (type::title, fonts::Weight::semiBold).withExtraKerningFactor (type::titleTracking));
    g.drawText (title.toUpperCase(), header, juce::Justification::centredLeft);

    g.setColour (colour::borderSubtle);
    g.drawVerticalLine (getWidth() - 1, 0.0f, static_cast<float> (getHeight()));
}

void ModulePanel::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (size::moduleHeaderHeight).reduced (space::s4, 0);
    toggle.setBounds (header.removeFromRight (size::toggleWidth + space::s2)
                          .withSizeKeepingCentre (size::toggleWidth + space::s2, size::controlHeight));
    header.removeFromRight (space::s2);
    resetButton.setBounds (
        header.removeFromRight (size::iconButton).withSizeKeepingCentre (size::iconButton, size::iconButton));
    header.removeFromRight (space::s4);

    // What is left after the title, for a panel's header controls.
    const auto titleFont =
        fonts::ui (type::title, fonts::Weight::semiBold).withExtraKerningFactor (type::titleTracking);
    header.removeFromLeft (juce::GlyphArrangement::getStringWidthInt (titleFont, title.toUpperCase()) + space::s6);
    layoutHeader (header);

    body.setBounds (area.reduced (space::s4, 0).withTrimmedBottom (space::s3));
    layoutBody (body.getLocalBounds());
}

// ---- Pitch (UI.md §11.1)
// -----------------------------------------------------------------------------------------------

PitchPanel::PitchPanel (juce::AudioProcessorValueTreeState& state)
    : ModulePanel (state, "Pitch", ParamId::pitchOn)
    , mode (addSegmented (ParamId::pitchMode))
    , octave (addKnob (ParamId::pitchOctave, "Octave"))
    , interval (addKnob (ParamId::pitchInterval, "Interval"))
    , direction (addSegmented (ParamId::pitchDirection))
    , semitones (addKnob (ParamId::pitchSemitones, "Semitones"))
    , fine (addKnob (ParamId::pitchFine, "Fine"))
    , mix (addKnob (ParamId::pitchMix, "Mix"))
    , formants (addCheckbox (ParamId::pitchFormant, "Preserve formants"))
    , modeWatcher (state, ParamId::pitchMode, [this] (float m) { showControlsForMode (juce::roundToInt (m)); })
{
}

void PitchPanel::showControlsForMode (int m)
{
    currentMode = m;
    const auto modeIs = [m] (params::PitchMode pm) { return m == static_cast<int> (params::enumIndex (pm)); };
    octave.setVisible (modeIs (params::PitchMode::Octave));
    interval.setVisible (modeIs (params::PitchMode::Interval));
    direction.setVisible (modeIs (params::PitchMode::Interval));
    semitones.setVisible (modeIs (params::PitchMode::Semitone));
    resized();
}

void PitchPanel::layoutBody (juce::Rectangle<int> area)
{
    auto row1 = area.removeFromTop (size::controlHeight);
    mode.setBounds (row1.withWidth (mode.getNumSegments() * size::segmentedSegment));
    area.removeFromTop (space::s3);

    const auto s = ThunderKnob::Size::medium;
    auto row2 = area.removeFromTop (knobHeight (s));
    const auto isInterval = currentMode == static_cast<int> (params::enumIndex (params::PitchMode::Interval));

    // The first knob depends on the mode; Interval also has Direction.
    for (auto* first : {&octave, &interval, &semitones})
        first->setBounds (knobIn (cell (row2, 0), s));

    direction.setBounds (cell (row2, 1).withSizeKeepingCentre (2 * size::segmentedSegment, size::controlHeight));
    fine.setBounds (knobIn (cell (row2, isInterval ? 2 : 1), s));
    mix.setBounds (knobIn (cell (row2, isInterval ? 3 : 2), s));

    area.removeFromTop (space::s3);
    formants.setBounds (area.removeFromTop (size::controlHeight).withWidth (size::checkboxRowWidth));
}

// ---- Time (§11.2)
// ------------------------------------------------------------------------------------------------------

TimePanel::TimePanel (juce::AudioProcessorValueTreeState& state)
    : ModulePanel (state, "Time", ParamId::timeOn)
    , speed (addSegmented (ParamId::halftimeSpeed))
    , smooth (addKnob (ParamId::halftimeSmooth, "Smooth"))
{
}

void TimePanel::layoutBody (juce::Rectangle<int> area)
{
    speed.setBounds (
        area.removeFromTop (size::controlHeight).withWidth (speed.getNumSegments() * size::segmentedSegment));
    area.removeFromTop (space::s3);
    smooth.setBounds (
        knobIn (cell (area.removeFromTop (knobHeight (ThunderKnob::Size::medium)), 0), ThunderKnob::Size::medium));
}

// ---- Grains (§11.3)
// ----------------------------------------------------------------------------------------------------

GrainsPanel::GrainsPanel (juce::AudioProcessorValueTreeState& state, const VisualModel& visuals)
    : ModulePanel (state, "Grains", ParamId::grainOn)
    , amount (addKnob (ParamId::grainAmount, "Amount", ThunderKnob::Size::large))
    , grainSize (addKnob (ParamId::grainSize, "Size"))
    , density (addKnob (ParamId::grainDensity, "Density"))
    , position (addKnob (ParamId::grainPosSpread, "Position"))
    , pitchSpread (addKnob (ParamId::grainPitchSpread, "Pitch spread"))
    , panSpread (addKnob (ParamId::grainPanSpread, "Pan spread"))
    , reverse (addKnob (ParamId::grainReverse, "Reverse"))
    , scaleLock (state, ParamId::grainScaleLock, "Scale lock")
    , tempoSync (state, ParamId::grainSync, "Tempo sync")
    , freeze (state, ParamId::grainFreeze, "Freeze", colour::accentPrimary)
    , cloud (visuals, state)
{
    for (juce::Component* c : {static_cast<juce::Component*> (&scaleLock), static_cast<juce::Component*> (&tempoSync),
                               static_cast<juce::Component*> (&freeze)})
        addAndMakeVisible (*c);
    body.addAndMakeVisible (cloud);
}

std::uint32_t GrainsPanel::wantedParts() const noexcept
{
    return UiBridge::kGrains;
}

void GrainsPanel::refresh (const VisualModel::Changes& changes)
{
    if (changes.grains)
        cloud.repaint();
}

void GrainsPanel::layoutHeader (juce::Rectangle<int> area)
{
    const auto checkbox = size::checkboxRowWidth * 2 / 3;
    scaleLock.setBounds (area.removeFromLeft (checkbox).withSizeKeepingCentre (checkbox, size::controlHeight));
    tempoSync.setBounds (area.removeFromLeft (checkbox).withSizeKeepingCentre (checkbox, size::controlHeight));
    area.removeFromLeft (space::s2);
    freeze.setBounds (
        area.removeFromLeft (size::chipWidth).withSizeKeepingCentre (size::chipWidth, size::controlHeight));
}

void GrainsPanel::layoutBody (juce::Rectangle<int> area)
{
    auto row1 = area.removeFromTop (knobHeight (ThunderKnob::Size::large));
    amount.setBounds (knobIn (cell (row1, 0), ThunderKnob::Size::large));
    grainSize.setBounds (knobIn (cell (row1, 1), ThunderKnob::Size::medium));
    density.setBounds (knobIn (cell (row1, 2), ThunderKnob::Size::medium));
    position.setBounds (knobIn (cell (row1, 3), ThunderKnob::Size::medium));

    area.removeFromTop (space::s2);
    auto row2 = area.removeFromTop (knobHeight (ThunderKnob::Size::medium));
    pitchSpread.setBounds (knobIn (cell (row2, 0), ThunderKnob::Size::medium));
    panSpread.setBounds (knobIn (cell (row2, 1), ThunderKnob::Size::medium));
    reverse.setBounds (knobIn (cell (row2, 2), ThunderKnob::Size::medium));
    cloud.setBounds (cell (row2, 3).withSizeKeepingCentre (size::grainCloudWidth, size::grainCloudHeight));
}

// ---- Static (§11.4)
// ----------------------------------------------------------------------------------------------------

StaticPanel::StaticPanel (juce::AudioProcessorValueTreeState& state, const VisualModel& visuals)
    : ModulePanel (state, "Static", ParamId::staticOn)
    , character (addSegmented (ParamId::staticCharacter))
    , amount (addKnob (ParamId::staticAmount, "Amount", ThunderKnob::Size::large))
    , rate (addKnob (ParamId::staticRate, "Rate"))
    , rateSynced (addKnob (ParamId::staticRateSynced, "Rate"))
    , sync (addCheckbox (ParamId::staticSync, "Sync"))
    , syncWatcher (state, ParamId::staticSync,
                   [this] (float on)
                   {
                       // One Rate knob at a time: Hz, or a note value while synced.
                       rate.setVisible (on < 0.5f);
                       rateSynced.setVisible (on >= 0.5f);
                   })
    , trace (visuals, state)
{
    character.setAccent (colour::accentSecondary);
    body.addAndMakeVisible (trace);

    const std::array<std::pair<ParamId, const char*>, 5> chips{{{ParamId::staticTgtPosition, "Position"},
                                                                {ParamId::staticTgtPitch, "Pitch"},
                                                                {ParamId::staticTgtFilter, "Filter"},
                                                                {ParamId::staticTgtPan, "Pan"},
                                                                {ParamId::staticTgtLevel, "Level"}}};
    for (std::size_t i = 0; i < chips.size(); ++i)
        targets[i] = &addChip (chips[i].first, chips[i].second, colour::accentSecondary);
}

void StaticPanel::layoutBody (juce::Rectangle<int> area)
{
    character.setBounds (
        area.removeFromTop (size::controlHeight).withWidth (character.getNumSegments() * size::segmentedSegment));
    area.removeFromTop (space::s3);

    const auto large = ThunderKnob::Size::large;
    auto row = area.removeFromTop (knobHeight (large));
    amount.setBounds (knobIn (cell (row, 0), large));
    rate.setBounds (knobIn (cell (row, 1), ThunderKnob::Size::medium));
    rateSynced.setBounds (rate.getBounds());
    sync.setBounds (cell (row, 2).withSizeKeepingCentre (size::checkboxRowWidth / 2, size::controlHeight));

    // The trace: as wide as the last column allows (UI.md §11.4 asks for 200).
    const auto traceCell = cell (row, 3);
    trace.setBounds (traceCell.withSizeKeepingCentre (std::min (size::staticTraceWidth, traceCell.getWidth()),
                                                      size::staticTraceHeight));

    area.removeFromTop (space::s3);
    auto chipRow = area.removeFromTop (size::controlHeight);
    for (auto* chip : targets)
    {
        chip->setBounds (chipRow.removeFromLeft (size::chipWidth));
        chipRow.removeFromLeft (size::chipGap);
    }
}

void StaticPanel::refresh (const VisualModel::Changes& changes)
{
    if (changes.modules)
        trace.repaint();
}

// ---- Space (§11.5)
// -----------------------------------------------------------------------------------------------------

SpacePanel::SpacePanel (juce::AudioProcessorValueTreeState& state)
    : ModulePanel (state, "Space", ParamId::spaceOn)
    , mode (addSegmented (ParamId::spaceMode))
    , mix (addKnob (ParamId::spaceMix, "Mix"))
    , decay (addKnob (ParamId::spaceDecay, "Decay"))
    , preDelay (addKnob (ParamId::spacePreDelay, "Pre-delay"))
    , damping (addKnob (ParamId::spaceDamping, "Damping"))
    , modeWatcher (state, ParamId::spaceMode,
                   [this] (float m)
                   {
                       currentMode = static_cast<params::SpaceMode> (juce::roundToInt (m));
                       decay.refreshValueText();
                   })
{
    // Decay reads as the decay time of the current mode (UI.md §11.5).
    decay.setValueFormatter (
        [this] (double percent)
        { return decayText (dsp::SpaceModule::decaySeconds (currentMode, static_cast<float> (percent))); });
}

juce::String SpacePanel::decayText (double seconds)
{
    if (seconds < 9.95)
        return juce::String (seconds, 1) + " s";
    return juce::String (juce::roundToInt (seconds)) + " s";
}

void SpacePanel::layoutBody (juce::Rectangle<int> area)
{
    mode.setBounds (
        area.removeFromTop (size::controlHeight).withWidth (mode.getNumSegments() * size::segmentedSegment));
    area.removeFromTop (space::s3);

    auto row = area.removeFromTop (knobHeight (ThunderKnob::Size::medium));
    mix.setBounds (knobIn (cell (row, 0), ThunderKnob::Size::medium));
    decay.setBounds (knobIn (cell (row, 1), ThunderKnob::Size::medium));
    preDelay.setBounds (knobIn (cell (row, 2), ThunderKnob::Size::medium));
    damping.setBounds (knobIn (cell (row, 3), ThunderKnob::Size::medium));
}

// ---- Tone (§11.6)
// ------------------------------------------------------------------------------------------------------

TonePanel::TonePanel (juce::AudioProcessorValueTreeState& state, const VisualModel& visuals)
    : ModulePanel (state, "Tone", ParamId::toneOn)
    , highPass (addKnob (ParamId::hpFreq, "High-pass"))
    , highPassSlope (addSegmented (ParamId::hpSlope, {}, {"12", "24"}))
    , lowPass (addKnob (ParamId::lpFreq, "Low-pass"))
    , lowPassSlope (addSegmented (ParamId::lpSlope, {}, {"12", "24"}))
    , curve (visuals, state, true)
{
    body.addAndMakeVisible (curve);
    body.addAndMakeVisible (openSpectrum);
    openSpectrum.setTooltip ("Show the spectrum and the filter curve in the visualiser.");
    openSpectrum.onClick = [this]
    {
        if (onOpenSpectrum)
            onOpenSpectrum();
    };
}

void TonePanel::layoutBody (juce::Rectangle<int> area)
{
    auto row = area.removeFromTop (knobHeight (ThunderKnob::Size::medium));
    const auto slope = [] (juce::Rectangle<int> c)
    { return c.withSizeKeepingCentre (2 * size::segmentedSegment, size::controlHeight); };

    highPass.setBounds (knobIn (cell (row, 0), ThunderKnob::Size::medium));
    highPassSlope.setBounds (slope (cell (row, 1)));
    lowPass.setBounds (knobIn (cell (row, 2), ThunderKnob::Size::medium));
    lowPassSlope.setBounds (slope (cell (row, 3)));

    area.removeFromTop (space::s2);
    auto row2 = area.removeFromTop (size::toneCurveHeight);
    curve.setBounds (row2.removeFromLeft (size::toneCurveWidth));
    row2.removeFromLeft (space::s4);
    openSpectrum.setBounds (row2.removeFromLeft (size::dialogButtonWidth * 2)
                                .withSizeKeepingCentre (size::dialogButtonWidth * 2, size::controlHeight));
}

std::uint32_t TonePanel::wantedParts() const noexcept
{
    return UiBridge::kSpectrum; // the curve's spectrum
}

void TonePanel::refresh (const VisualModel::Changes& changes)
{
    if (changes.spectrum)
        curve.repaint();
}

// ---- Width (§11.7)
// -----------------------------------------------------------------------------------------------------

WidthPanel::WidthPanel (juce::AudioProcessorValueTreeState& state, const VisualModel& visuals)
    : ModulePanel (state, "Width", ParamId::widthOn)
    , width (addKnob (ParamId::widthAmount, "Width", ThunderKnob::Size::large))
    , autopan (addCheckbox (ParamId::autopanOn, "Autopan"))
    , autopanRate (addDropdown (ParamId::autopanRate))
    , autopanDepth (addKnob (ParamId::autopanDepth, "Depth"))
    , correlation (visuals.getMeters())
{
    setUpCaption (autopanRateCaption, "RATE");
    body.addAndMakeVisible (autopanRateCaption);
    setUpCaption (correlationCaption, "CORRELATION");
    body.addAndMakeVisible (correlationCaption);
    body.addAndMakeVisible (correlation);
}

void WidthPanel::refresh (const VisualModel::Changes& changes)
{
    if (changes.meters)
        correlation.repaint();
}

void WidthPanel::layoutBody (juce::Rectangle<int> area)
{
    const auto large = ThunderKnob::Size::large;
    const auto medium = ThunderKnob::Size::medium;
    auto row1 = area.removeFromTop (knobHeight (large));
    width.setBounds (knobIn (cell (row1, 0), large));

    // The correlation of the output, larger than the Output panel's (UI.md
    // §11.7 row 3, here beside Width: the body has no third row).
    auto meterArea = cell (row1, 1, 2);
    correlationCaption.setBounds (meterArea.removeFromTop (size::labelHeight));
    correlation.setBounds (meterArea.withSizeKeepingCentre (meterArea.getWidth(), size::correlationMiniHeight));

    // Row 2 (UI.md §11.7): Autopan, its rate and depth.
    area.removeFromTop (space::s2);
    auto row = area.removeFromTop (knobHeight (medium));
    autopan.setBounds (cell (row, 0).withSizeKeepingCentre (size::checkboxRowWidth / 2, size::controlHeight));
    auto rateCell = cell (row, 1);
    autopanRateCaption.setBounds (rateCell.removeFromTop (size::labelHeight));
    autopanRate.setBounds (rateCell.withSizeKeepingCentre (size::rateDropdownWidth, size::controlHeight));
    autopanDepth.setBounds (knobIn (cell (row, 2), medium));
}

// ---- Duck (§11.9)
// ------------------------------------------------------------------------------------------------------

DuckPanel::DuckPanel (juce::AudioProcessorValueTreeState& state, const VisualModel& visuals)
    : ModulePanel (state, "Duck", ParamId::duckOn)
    , mode (addSegmented (ParamId::duckMode, {static_cast<int> (params::enumIndex (params::DuckMode::Envelope))}))
    , amount (addKnob (ParamId::duckAmount, "Amount"))
    , release (addKnob (ParamId::duckRelease, "Release"))
    , meter (visuals, state)
{
    setUpCaption (meterCaption, "REDUCTION");
    body.addAndMakeVisible (meterCaption);
    body.addAndMakeVisible (meter);
}

void DuckPanel::refresh (const VisualModel::Changes& changes)
{
    const auto reduction = meter.getValueText();
    if (changes.modules && reduction != shownReduction)
    {
        shownReduction = reduction;
        meter.repaint();
    }
}

void DuckPanel::layoutBody (juce::Rectangle<int> area)
{
    mode.setBounds (
        area.removeFromTop (size::controlHeight).withWidth (mode.getNumSegments() * size::segmentedSegment));
    area.removeFromTop (space::s3);

    const auto medium = ThunderKnob::Size::medium;
    auto row = area.removeFromTop (knobHeight (medium));
    amount.setBounds (knobIn (cell (row, 0), medium));
    release.setBounds (knobIn (cell (row, 1), medium));

    auto meterArea = cell (row, 2, 2);
    meterCaption.setBounds (meterArea.removeFromTop (size::labelHeight));
    meter.setBounds (meterArea.withSizeKeepingCentre (meterArea.getWidth(), size::reductionMeterHeight));
}

// ---- Shape (§11.8)
// -----------------------------------------------------------------------------------------------------

ShapePanel::ShapePanel (juce::AudioProcessorValueTreeState& state, const VisualModel& visuals)
    : ModulePanel (state, "Shape", ParamId::shapeOn)
    , shape (addSegmented (ParamId::shapeType))
    , rate (addDropdown (ParamId::shapeRate))
    , depth (addKnob (ParamId::shapeDepth, "Depth"))
    , smooth (addKnob (ParamId::shapeSmooth, "Smooth"))
    , display (visuals, state)
    , typeWatcher (state, ParamId::shapeType, [this] (float) { display.repaint(); })
    , depthWatcher (state, ParamId::shapeDepth, [this] (float) { display.repaint(); })
    , smoothWatcher (state, ParamId::shapeSmooth, [this] (float) { display.repaint(); })
    , visualModel (visuals)
{
    body.addAndMakeVisible (display);

    // Each segment draws its curve over one cycle (full level at the top).
    shape.setOptionPainter (
        [] (juce::Graphics& g, int option, juce::Rectangle<float> area, juce::Colour ink)
        {
            const auto type = static_cast<params::ShapeType> (option);
            const auto steps = std::max (2, static_cast<int> (area.getWidth()) * 2);

            juce::Path curve;
            for (int i = 0; i <= steps; ++i)
            {
                // Just inside the cycle at the end, so a curve that resets
                // at the next cycle shows its last value.
                const auto phase = std::min (static_cast<double> (i) / steps, 0.9999);
                const auto x = area.getX() + area.getWidth() * static_cast<float> (i) / static_cast<float> (steps);
                const auto y = area.getBottom() - area.getHeight() * dsp::ShapeModule::shapeAt (type, phase);
                if (i == 0)
                    curve.startNewSubPath (x, y);
                else
                    curve.lineTo (x, y);
            }

            g.setColour (ink);
            g.strokePath (curve, juce::PathStrokeType (size::shapeThumbStroke, juce::PathStrokeType::mitered));
        });

    setUpCaption (rateCaption, "RATE");
    body.addAndMakeVisible (rateCaption);
}

void ShapePanel::refresh (const VisualModel::Changes& changes)
{
    // The playhead moves only while the song does (or Shape counts its own time).
    const auto& activity = visualModel.getActivity();
    if (changes.modules && !reduceMotion::isOn() &&
        (!juce::exactlyEqual (activity.shapePhase, shownPhase) || !juce::exactlyEqual (activity.shapeGain, shownGain)))
    {
        shownPhase = activity.shapePhase;
        shownGain = activity.shapeGain;
        display.repaint();
    }
}

void ShapePanel::layoutBody (juce::Rectangle<int> area)
{
    const auto thumb = size::shapeThumbWidth + 2 * space::s1;
    shape.setBounds (
        area.removeFromTop (size::shapeThumbHeight + 2 * space::s1).withWidth (shape.getNumSegments() * thumb));
    area.removeFromTop (space::s3);

    const auto medium = ThunderKnob::Size::medium;
    auto row = area.removeFromTop (knobHeight (medium));

    // The dropdown lines up with the knobs' dials, its caption with their labels.
    auto rateCell = cell (row, 0);
    rateCaption.setBounds (rateCell.removeFromTop (size::labelHeight));
    rate.setBounds (rateCell.withSizeKeepingCentre (size::rateDropdownWidth, size::controlHeight));

    depth.setBounds (knobIn (cell (row, 1), medium));
    smooth.setBounds (knobIn (cell (row, 2), medium));

    area.removeFromTop (space::s3);
    display.setBounds (area.removeFromTop (size::shapeDisplayHeight).withWidth (size::shapeDisplayWidth));
}

} // namespace thunder::ui
