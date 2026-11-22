#include "ModulePanels.h"

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

// ---- Grains (§11.3)
// ----------------------------------------------------------------------------------------------------

GrainsPanel::GrainsPanel (juce::AudioProcessorValueTreeState& state)
    : ModulePanel (state, "Grains", ParamId::grainOn)
    , amount (addKnob (ParamId::grainAmount, "Amount", ThunderKnob::Size::large))
    , grainSize (addKnob (ParamId::grainSize, "Size"))
    , density (addKnob (ParamId::grainDensity, "Density"))
    , position (addKnob (ParamId::grainPosSpread, "Position"))
    , pitchSpread (addKnob (ParamId::grainPitchSpread, "Pitch spread"))
    , panSpread (addKnob (ParamId::grainPanSpread, "Pan spread"))
    , reverse (addKnob (ParamId::grainReverse, "Reverse"))
{
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
}

// ---- Space (§11.5)
// -----------------------------------------------------------------------------------------------------

SpacePanel::SpacePanel (juce::AudioProcessorValueTreeState& state)
    : ModulePanel (state, "Space", ParamId::spaceOn)
    , mode (addSegmented (ParamId::spaceMode, {static_cast<int> (params::enumIndex (params::SpaceMode::Cloud))}))
    , mix (addKnob (ParamId::spaceMix, "Mix"))
    , decay (addKnob (ParamId::spaceDecay, "Decay"))
    , preDelay (addKnob (ParamId::spacePreDelay, "Pre-delay"))
    , damping (addKnob (ParamId::spaceDamping, "Damping"))
{
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

// ---- Width (§11.7)
// -----------------------------------------------------------------------------------------------------

WidthPanel::WidthPanel (juce::AudioProcessorValueTreeState& state)
    : ModulePanel (state, "Width", ParamId::widthOn)
    , width (addKnob (ParamId::widthAmount, "Width", ThunderKnob::Size::large))
{
}

void WidthPanel::layoutBody (juce::Rectangle<int> area)
{
    width.setBounds (
        knobIn (cell (area.removeFromTop (knobHeight (ThunderKnob::Size::large)), 0), ThunderKnob::Size::large));
}

} // namespace thunder::ui
