#include "ThunderEditor.h"

#include "ThunderProcessor.h"
#include "core/state/UserSettings.h"
#include "ui/theme/ThunderFonts.h"
#include "ui/theme/ThunderIcons.h"
#include "ui/theme/ThunderPaint.h"
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

// The work area of the main display, or nothing when there is no display.
juce::Rectangle<int> primaryWorkArea()
{
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        return display->userBounds.toNearestInt();
    return {};
}

// Deletes an overlay after the callback that asked for it has returned.
template<typename Overlay>
void closeLater (ThunderEditor& editor, std::unique_ptr<Overlay>& overlay, std::function<void()> after = {})
{
    juce::MessageManager::callAsync (
        [safe = juce::Component::SafePointer<ThunderEditor> (&editor), &overlay, after]
        {
            if (safe == nullptr)
                return;
            overlay.reset();
            if (after)
                after();
        });
}
} // namespace

// ---- Content
// ---------------------------------------------------------------------------------------------------------

ThunderEditor::Content::Content (ThunderEditor& o)
    : owner (o)
{
    setOpaque (true);
}

void ThunderEditor::Content::paint (juce::Graphics& g)
{
    // Everything here is static: painted once per size, then drawn as one image.
    staticLayer.draw (g, getLocalBounds(), [this] (juce::Graphics& layer) { paintStatic (layer); });
}

void ThunderEditor::Content::paintStatic (juce::Graphics& g)
{
    ui::paint::paintBackground (g, getLocalBounds().toFloat());

    // The visualiser well (UI.md §6.2): recessed, a window into the storm.
    const auto region = getLocalBounds().withTrimmedTop (size::topBarHeight).withHeight (size::visualiserHeight);
    const auto well = visualiserWell (region).toFloat();
    g.setColour (colour::bgDeep);
    g.fillRoundedRectangle (well, radius::md);

    // Inner shadow along the top edge.
    g.setGradientFill (juce::ColourGradient::vertical (colour::shadow.withAlpha (elevation::wellShadow), well.getY(),
                                                       colour::shadow.withAlpha (0.0f), well.getY() + space::s2));
    g.fillRoundedRectangle (well.withHeight (static_cast<float> (space::s2)), radius::md);
    g.setColour (colour::shadow.withAlpha (elevation::wellShadow));
    g.drawRoundedRectangle (well.reduced (border::width * 1.5f), radius::md,
                            static_cast<float> (elevation::wellShadowSize));
    g.setColour (colour::borderSubtle);
    g.drawRoundedRectangle (well.reduced (border::width * 0.5f), radius::md, border::width);

    // Thunder's bolt until the visualiser arrives (ROADMAP T-0.6-04/05).
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

    for (auto* overlay : std::initializer_list<juce::Component*>{owner.saveForm.get(), owner.settingsPanel.get(),
                                                                 owner.aboutPanel.get()})
        if (overlay != nullptr)
            overlay->setBounds (getLocalBounds());
}

// ---- ScaleConstrainer
// ------------------------------------------------------------------------------------------------

void ThunderEditor::ScaleConstrainer::checkBounds (juce::Rectangle<int>& bounds, const juce::Rectangle<int>& previous,
                                                   const juce::Rectangle<int>& limits, bool stretchingTop,
                                                   bool stretchingLeft, bool stretchingBottom, bool stretchingRight)
{
    juce::ComponentBoundsConstrainer::checkBounds (bounds, previous, limits, stretchingTop, stretchingLeft,
                                                   stretchingBottom, stretchingRight);

    const auto snapped = snapScale (static_cast<float> (bounds.getWidth()) / size::baseWidth);
    const auto w = juce::roundToInt (size::baseWidth * snapped);
    const auto h = juce::roundToInt (size::baseHeight * snapped);

    // Keep the edge that isn't being dragged where it is.
    bounds = juce::Rectangle<int> (stretchingLeft ? bounds.getRight() - w : bounds.getX(),
                                   stretchingTop ? bounds.getBottom() - h : bounds.getY(), w, h);
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
{
    setLookAndFeel (&lookAndFeel);
    setTitle ("Thunder");

    auto& apvts = p.getValueTreeState();
    modules.push_back (std::make_unique<ui::PitchPanel> (apvts));
    modules.push_back (std::make_unique<ui::GrainsPanel> (apvts));
    modules.push_back (std::make_unique<ui::SpacePanel> (apvts));
    modules.push_back (std::make_unique<ui::TonePanel> (apvts));
    modules.push_back (std::make_unique<ui::WidthPanel> (apvts));

    // Children in reading order, which is also the keyboard order (UI.md §14).
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
    topBar.onSettings = [this] { openSettings(); };
    topBar.onAbout = [this] { openAbout(); };

    // Status (UI.md §8.6): the latency the host compensates for, and the version.
    const auto sampleRate = p.getSampleRate() > 0.0 ? p.getSampleRate() : 48000.0;
    status.setText ("LAT " + juce::String (juce::roundToInt (1000.0 * p.getLatencySamples() / sampleRate)) + " ms  " +
                        juce::String::fromUTF8 ("\xc2\xb7") + "  v" + THUNDER_VERSION_STRING,
                    juce::dontSendNotification);
    status.setFont (ui::fonts::ui (type::caption));
    status.setColour (juce::Label::textColourId, colour::textSecondary);
    status.setJustificationType (juce::Justification::centredRight);
    status.setTitle ("Latency and version");

    content.setSize (size::baseWidth, size::baseHeight);
    selectModule (0);

    const auto settings = state::UserSettings::load (p.getSettingsFile());
    showTooltips (settings.showTooltips);

    // Fixed aspect ratio, 75–200%, 5% steps (UI.md §7.1).
    scaleConstrainer.setSizeLimits (
        juce::roundToInt (size::baseWidth * size::minScale), juce::roundToInt (size::baseHeight * size::minScale),
        juce::roundToInt (size::baseWidth * size::maxScale), juce::roundToInt (size::baseHeight * size::maxScale));
    scaleConstrainer.setFixedAspectRatio (static_cast<double> (size::baseWidth) / size::baseHeight);
    setConstrainer (&scaleConstrainer);
    setResizable (true, true);

    // The project's size, else the last one chosen, else 100% — made smaller
    // if it wouldn't fit the screen.
    const auto session = p.getSessionState();
    const auto wanted = session.uiScale > 0.0f ? session.uiScale : (settings.uiScale > 0.0f ? settings.uiScale : 1.0f);
    scale = fittingScale (wanted, primaryWorkArea());
    setSize (juce::roundToInt (size::baseWidth * scale), juce::roundToInt (size::baseHeight * scale));

    // GPU rendering on Windows and macOS (PRD DEC-004, DEC-048, UI-FX-004). JUCE
    // paints the components into the context, so nothing else changes. Not on
    // Linux: the context's render thread makes X11 calls alongside the host's,
    // which aborts inside Xlib ("xcb: unknown sequence number"), so JUCE's own
    // renderer draws there.
    if constexpr (useOpenGL)
    {
        openGL.setComponentPaintingEnabled (true);
        openGL.setContinuousRepainting (false);
        openGL.attachTo (*this);
    }

    constructed = true;
}

ThunderEditor::~ThunderEditor()
{
    openGL.detach();
    setLookAndFeel (nullptr);
}

float ThunderEditor::snapScale (float s) noexcept
{
    const auto steps = std::round (s / size::scaleStep);
    return juce::jlimit (size::minScale, size::maxScale, static_cast<float> (steps) * size::scaleStep);
}

float ThunderEditor::fittingScale (float wanted, juce::Rectangle<int> workArea) noexcept
{
    wanted = snapScale (wanted);
    if (workArea.isEmpty())
        return wanted;

    const auto fitWidth = static_cast<float> (workArea.getWidth() - size::hostFrameWidth) / size::baseWidth;
    const auto fitHeight = static_cast<float> (workArea.getHeight() - size::hostFrameHeight) / size::baseHeight;

    // The largest 5% step that fits (a hair of tolerance for float rounding).
    const auto fit = std::floor (std::min (fitWidth, fitHeight) / size::scaleStep + 1.0e-3f) * size::scaleStep;
    return juce::jlimit (size::minScale, size::maxScale, std::min (wanted, fit));
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

    // Sizes the user (or the host) chooses after opening are the project's.
    if (constructed)
        rememberScale();
}

void ThunderEditor::rememberScale()
{
    auto session = thunderProcessor.getSessionState();
    if (!juce::exactlyEqual (session.uiScale, scale))
    {
        session.uiScale = scale;
        thunderProcessor.setSessionState (session);
    }
}

void ThunderEditor::setScale (float newScale)
{
    const auto s = snapScale (newScale);
    setSize (juce::roundToInt (size::baseWidth * s), juce::roundToInt (size::baseHeight * s));

    // The default for projects that don't have their own size yet.
    auto settings = state::UserSettings::load (thunderProcessor.getSettingsFile());
    settings.uiScale = s;
    settings.save (thunderProcessor.getSettingsFile());
}

void ThunderEditor::showTooltips (bool show)
{
    if (show && tooltips == nullptr)
        tooltips = std::make_unique<juce::TooltipWindow> (this, timing::tooltipDelayMs);
    else if (!show)
        tooltips.reset();
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
        closeLater (*this, saveForm,
                    [this, saved]
                    {
                        topBar.getPresetBar().refresh();
                        if (saved)
                            showMessage ("Preset saved", false);
                    });
    };
    content.addAndMakeVisible (*saveForm);
    saveForm->setBounds (content.getLocalBounds());
    saveForm->grabKeyboardFocus();
    saveForm->getNameField().grabKeyboardFocus();
}

void ThunderEditor::openSettings()
{
    if (settingsPanel != nullptr)
        return;

    settingsPanel = std::make_unique<ui::SettingsPanel> (thunderProcessor, scale);
    settingsPanel->onScaleChosen = [this] (float s) { setScale (s); };
    settingsPanel->onShowTooltips = [this] (bool show) { showTooltips (show); };
    settingsPanel->onClose = [this] { closeLater (*this, settingsPanel); };
    content.addAndMakeVisible (*settingsPanel);
    settingsPanel->setBounds (content.getLocalBounds());
    settingsPanel->grabKeyboardFocus();
}

void ThunderEditor::openAbout()
{
    if (aboutPanel != nullptr)
        return;

    aboutPanel = std::make_unique<ui::AboutPanel>();
    aboutPanel->onClose = [this] { closeLater (*this, aboutPanel); };
    content.addAndMakeVisible (*aboutPanel);
    aboutPanel->setBounds (content.getLocalBounds());
    aboutPanel->grabKeyboardFocus();
}

void ThunderEditor::showMessage (const juce::String& text, bool isError)
{
    toast.show (text, isError);
}

} // namespace thunder
