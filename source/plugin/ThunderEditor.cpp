#include "ThunderEditor.h"

#include "ThunderProcessor.h"
#include "ui/theme/ThunderFonts.h"
#include "ui/theme/ThunderIcons.h"
#include "ui/theme/ThunderTheme.h"

#include <cmath>

namespace thunder
{

using namespace ui::theme;
using params::ParamId;

namespace
{
juce::Rectangle<int> visualiserWell (juce::Rectangle<int> area)
{
    return area.reduced (space::s4, space::s3);
}
} // namespace

// ---- Content
// ---------------------------------------------------------------------------------------------------------

ThunderEditor::Content::Content (ThunderEditor& o)
    : owner (o)
{
}

void ThunderEditor::Content::paint (juce::Graphics& g)
{
    g.fillAll (colour::bgBase);

    // The visualiser well, with Thunder's bolt until the visualiser arrives.
    const auto region = getLocalBounds().withTrimmedTop (size::topBarHeight).withHeight (size::visualiserHeight);
    const auto well = visualiserWell (region).toFloat();
    g.setColour (colour::bgDeep);
    g.fillRoundedRectangle (well, radius::md);
    g.setColour (colour::borderSubtle);
    g.drawRoundedRectangle (well.reduced (border::width * 0.5f), radius::md, border::width);

    const auto glyph =
        juce::Rectangle<float> (static_cast<float> (size::placeholderBolt), static_cast<float> (size::placeholderBolt))
            .withCentre (well.getCentre());
    g.setColour (colour::accentPrimary.withAlpha (opacity::placeholder));
    g.fillPath (ui::icons::fitted (ui::icons::bolt(), glyph));

    g.setColour (colour::borderSubtle);
    g.drawHorizontalLine (region.getBottom(), 0.0f, static_cast<float> (getWidth()));

    // The bottom bar behind the status text (the capture widget paints its own part).
    const auto bottom = getLocalBounds().removeFromBottom (size::bottomBarHeight);
    g.setColour (colour::bgPanel);
    g.fillRect (bottom);
    g.setColour (colour::borderSubtle);
    g.drawHorizontalLine (bottom.getY(), 0.0f, static_cast<float> (getWidth()));
}

void ThunderEditor::Content::resized()
{
    auto area = getLocalBounds();
    owner.topBar.setBounds (area.removeFromTop (size::topBarHeight));

    const auto visualiser = area.removeFromTop (size::visualiserHeight);
    const auto well = visualiserWell (visualiser);
    owner.toast.setBounds (
        well.reduced (space::s3).removeFromTop (size::toastHeight).removeFromRight (size::toastWidth));

    owner.chain.setBounds (area.removeFromTop (size::chainHeight));

    auto bottom = area.removeFromBottom (size::bottomBarHeight);
    owner.status.setBounds (bottom.removeFromRight (size::statusWidth).withTrimmedRight (space::s5));
    owner.captureWidget.setBounds (bottom);

    owner.output.setBounds (area.removeFromRight (size::outputWidth));
    for (auto& m : owner.modules)
        m->setBounds (area);

    if (owner.saveForm != nullptr)
        owner.saveForm->setBounds (getLocalBounds());
}

// ---- ThunderEditor
// ---------------------------------------------------------------------------------------------------

ThunderEditor::ThunderEditor (ThunderProcessor& p)
    : AudioProcessorEditor (p)
    , thunderProcessor (p)
    , content (*this)
    , topBar (p.getValueTreeState(), p.getPresetManager())
    , chain (p.getValueTreeState(), {{"Pitch", ParamId::pitchOn},
                                     {"Grains", ParamId::grainOn},
                                     {"Space", ParamId::spaceOn},
                                     {"Tone", ParamId::toneOn},
                                     {"Width", ParamId::widthOn}})
    , output (p.getValueTreeState())
    , captureWidget (p)
    , tooltips (this, timing::tooltipDelayMs)
{
    setLookAndFeel (&lookAndFeel);

    auto& apvts = p.getValueTreeState();
    modules.push_back (std::make_unique<ui::PitchPanel> (apvts));
    modules.push_back (std::make_unique<ui::GrainsPanel> (apvts));
    modules.push_back (std::make_unique<ui::SpacePanel> (apvts));
    modules.push_back (std::make_unique<ui::TonePanel> (apvts));
    modules.push_back (std::make_unique<ui::WidthPanel> (apvts));

    content.addAndMakeVisible (topBar);
    content.addAndMakeVisible (chain);
    for (auto& m : modules)
        content.addChildComponent (*m);
    content.addAndMakeVisible (output);
    content.addAndMakeVisible (captureWidget);
    content.addAndMakeVisible (status);
    content.addChildComponent (toast);
    addAndMakeVisible (content);

    chain.onSelect = [this] (int index) { selectModule (index); };
    topBar.getPresetBar().onSave = [this] (bool saveAs) { openSaveForm (saveAs); };
    topBar.getPresetBar().onError = [this] (const juce::String& text) { showMessage (text, true); };

    // Status (UI.md §8.6): the latency the host compensates for, and the version.
    const auto sampleRate = p.getSampleRate() > 0.0 ? p.getSampleRate() : 48000.0;
    status.setText ("LAT " + juce::String (juce::roundToInt (1000.0 * p.getLatencySamples() / sampleRate)) + " ms  " +
                        juce::String::fromUTF8 ("\xc2\xb7") + "  v" + THUNDER_VERSION_STRING,
                    juce::dontSendNotification);
    status.setFont (ui::fonts::ui (type::caption));
    status.setColour (juce::Label::textColourId, colour::textSecondary);
    status.setJustificationType (juce::Justification::centredRight);

    content.setSize (size::baseWidth, size::baseHeight);
    selectModule (0);

    // Fixed aspect ratio, 75–200% (UI.md §7.1).
    setResizable (true, true);
    setResizeLimits (
        juce::roundToInt (size::baseWidth * size::minScale), juce::roundToInt (size::baseHeight * size::minScale),
        juce::roundToInt (size::baseWidth * size::maxScale), juce::roundToInt (size::baseHeight * size::maxScale));
    if (auto* aspect = getConstrainer())
        aspect->setFixedAspectRatio (static_cast<double> (size::baseWidth) / size::baseHeight);
    setSize (size::baseWidth, size::baseHeight);
}

ThunderEditor::~ThunderEditor()
{
    setLookAndFeel (nullptr);
}

void ThunderEditor::paint (juce::Graphics& g)
{
    g.fillAll (colour::bgBase);
}

void ThunderEditor::resized()
{
    scale = juce::jlimit (size::minScale, size::maxScale, static_cast<float> (getWidth()) / size::baseWidth);
    content.setTransform (juce::AffineTransform::scale (scale));
    content.setBounds (0, 0, size::baseWidth, size::baseHeight);
}

void ThunderEditor::selectModule (int index)
{
    index = juce::jlimit (0, getNumModules() - 1, index);
    chain.setSelected (index);
    for (int i = 0; i < getNumModules(); ++i)
        modules[static_cast<std::size_t> (i)]->setVisible (i == index);
}

void ThunderEditor::openSaveForm (bool saveAs)
{
    saveForm = std::make_unique<ui::SaveForm> (thunderProcessor.getPresetManager(), saveAs);
    saveForm->onClose = [this] (bool saved)
    {
        // Deleting the form from inside its own callback isn't safe; do it next.
        juce::MessageManager::callAsync (
            [safe = juce::Component::SafePointer<ThunderEditor> (this), saved]
            {
                if (safe == nullptr)
                    return;
                safe->saveForm.reset();
                safe->topBar.getPresetBar().refresh();
                if (saved)
                    safe->showMessage ("Preset saved", false);
            });
    };
    content.addAndMakeVisible (*saveForm);
    saveForm->setBounds (content.getLocalBounds());
    saveForm->grabKeyboardFocus();
    saveForm->getNameField().grabKeyboardFocus();
}

void ThunderEditor::showMessage (const juce::String& text, bool isError)
{
    toast.show (text, isError);
}

} // namespace thunder
