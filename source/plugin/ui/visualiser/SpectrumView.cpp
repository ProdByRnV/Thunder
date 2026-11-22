#include "SpectrumView.h"

#include "plugin/ui/ParamHelp.h"
#include "plugin/ui/ParamUi.h"
#include "plugin/ui/theme/ThunderFonts.h"
#include "plugin/ui/theme/ThunderTheme.h"

#include <algorithm>
#include <cmath>

namespace thunder::ui
{

using namespace theme;
using params::ParamId;

namespace
{
// Points along the filter curve.
constexpr int kCurvePoints = 192;

float realValue (const juce::RangedAudioParameter& p)
{
    return p.convertFrom0to1 (p.getValue());
}

juce::String frequencyLabel (float hz)
{
    return hz >= 1000.0f ? juce::String (juce::roundToInt (hz / 1000.0f)) + "k" : juce::String (juce::roundToInt (hz));
}
} // namespace

// ---- Handle
// ----------------------------------------------------------------------------------------------------------

// A cutoff handle on the curve: drag left or right; a screen reader sees a
// slider with the parameter's value.
class SpectrumView::Handle final : public juce::Component
{
public:
    Handle (SpectrumView& view, juce::RangedAudioParameter& p, ParamId paramId, const juce::String& name)
        : owner (view)
        , parameter (p)
        , id (paramId)
        , attachment (p, [this] (float) { owner.filterChanged(); }, nullptr)
    {
        setTitle (name);
        setDescription (help::describe (id));
        setHelpText (help::describe (id));
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        setRepaintsOnMouseActivity (true);
    }

    float getHz() const { return realValue (parameter); }
    juce::String getValueText() const { return parameter.getCurrentValueAsText(); }
    bool showsValue() const { return dragging || isMouseOver(); }

    // [MSG] What a drag to `x` (in the view) does; also used by tests.
    void dragTo (float x)
    {
        const auto range = parameter.getNormalisableRange();
        attachment.setValueAsPartOfGesture (std::clamp (owner.frequencyForX (x), range.start, range.end));
    }

    void paint (juce::Graphics& g) override
    {
        const auto d = static_cast<float> (size::filterHandle);
        const auto circle = getLocalBounds().toFloat().withSizeKeepingCentre (d, d);
        const auto active = showsValue();
        g.setColour (active ? colour::filterCurve : colour::bgRaised);
        g.fillEllipse (circle);
        g.setColour (colour::filterCurve);
        g.drawEllipse (circle.reduced (size::spectrumStroke * 0.5f), size::spectrumStroke);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        dragging = true;
        attachment.beginGesture();
        owner.repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override { dragTo (e.getEventRelativeTo (&owner).position.x); }

    void mouseUp (const juce::MouseEvent&) override
    {
        attachment.endGesture();
        dragging = false;
        owner.repaint();
    }

    void mouseEnter (const juce::MouseEvent&) override { owner.repaint(); }
    void mouseExit (const juce::MouseEvent&) override { owner.repaint(); }

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        class Value final : public juce::AccessibilityValueInterface
        {
        public:
            explicit Value (Handle& h)
                : handle (h)
            {
            }

            bool isReadOnly() const override { return false; }
            double getCurrentValue() const override { return handle.getHz(); }
            juce::String getCurrentValueAsString() const override { return handle.getValueText(); }
            void setValue (double newValue) override
            {
                handle.attachment.setValueAsCompleteGesture (static_cast<float> (newValue));
            }
            void setValueAsString (const juce::String& text) override
            {
                handle.attachment.setValueAsCompleteGesture (
                    handle.parameter.convertFrom0to1 (handle.parameter.getValueForText (text)));
            }
            AccessibleValueRange getRange() const override
            {
                const auto range = handle.parameter.getNormalisableRange();
                return {{range.start, range.end}, 0.0};
            }

        private:
            Handle& handle;
        };

        return std::make_unique<juce::AccessibilityHandler> (
            *this, juce::AccessibilityRole::slider, juce::AccessibilityActions{},
            juce::AccessibilityHandler::Interfaces{std::make_unique<Value> (*this)});
    }

private:
    SpectrumView& owner;
    juce::RangedAudioParameter& parameter;
    ParamId id;
    juce::ParameterAttachment attachment;
    bool dragging = false;
};

// ---- SpectrumView
// ----------------------------------------------------------------------------------------------------

SpectrumView::SpectrumView (const VisualModel& m, juce::AudioProcessorValueTreeState& state, bool isCompact)
    : model (m)
    , apvts (state)
    , compact (isCompact)
{
    // The view lets clicks through to the visualiser (right-click menu); only
    // the handles take them.
    setInterceptsMouseClicks (false, true);
    setTitle (compact ? "Filter curve" : "Spectrum");
    setDescription ("The spectrum of the dry signal and the layer, with the Tone module's filter curve. "
                    "Drag the handles to set the high-pass and low-pass cutoffs.");

    highPass =
        std::make_unique<Handle> (*this, parameter (apvts, ParamId::hpFreq), ParamId::hpFreq, "High-pass cutoff");
    lowPass = std::make_unique<Handle> (*this, parameter (apvts, ParamId::lpFreq), ParamId::lpFreq, "Low-pass cutoff");
    addAndMakeVisible (*highPass);
    addAndMakeVisible (*lowPass);

    for (const auto id : {ParamId::hpSlope, ParamId::lpSlope, ParamId::toneOn})
        watchers.push_back (
            std::make_unique<juce::ParameterAttachment> (parameter (apvts, id), [this] (float) { filterChanged(); }));

    filterChanged();
}

SpectrumView::~SpectrumView() = default;

juce::Component& SpectrumView::getHighPassHandle() noexcept
{
    return *highPass;
}

juce::Component& SpectrumView::getLowPassHandle() noexcept
{
    return *lowPass;
}

void SpectrumView::setShowDry (bool show)
{
    showDry = show;
    repaint();
}

float SpectrumView::xForFrequency (float hz) const noexcept
{
    const auto t =
        std::log (std::max (hz, 1.0f) / visualiser::minHz) / std::log (visualiser::maxHz / visualiser::minHz);
    return t * static_cast<float> (getWidth());
}

float SpectrumView::frequencyForX (float x) const noexcept
{
    const auto t = x / std::max (1.0f, static_cast<float> (getWidth()));
    return visualiser::minHz * std::pow (visualiser::maxHz / visualiser::minHz, t);
}

float SpectrumView::yForDecibels (float db) const noexcept
{
    const auto t = (visualiser::maxDb - db) / (visualiser::maxDb - visualiser::minDb);
    return t * static_cast<float> (getHeight());
}

float SpectrumView::curveY (float hz) const noexcept
{
    const auto db = filter.decibelsAt (hz, model.getSampleRate()) + visualiser::curveTopDb;
    return std::min (yForDecibels (std::max (db, visualiser::minDb - 6.0f)), static_cast<float> (getHeight()) + 4.0f);
}

void SpectrumView::filterChanged()
{
    const auto value = [this] (ParamId id) { return realValue (parameter (apvts, id)); };
    const auto steep = [&value] (ParamId id)
    { return juce::roundToInt (value (id)) == static_cast<int> (params::enumIndex (params::FilterSlope::Db24)); };

    filter.highPassHz = value (ParamId::hpFreq);
    filter.highPass24 = steep (ParamId::hpSlope);
    filter.lowPassHz = value (ParamId::lpFreq);
    filter.lowPass24 = steep (ParamId::lpSlope);
    toneOn = value (ParamId::toneOn) >= 0.5f;

    resized();
    repaint();
}

void SpectrumView::resized()
{
    if (highPass == nullptr)
        return;

    const auto hit = static_cast<float> (size::filterHandleHit);
    for (auto* handle : {highPass.get(), lowPass.get()})
    {
        const auto hz = handle->getHz();
        handle->setBounds (
            juce::Rectangle<float> (hit, hit).withCentre ({xForFrequency (hz), curveY (hz)}).toNearestInt());
        handle->setAlpha (toneOn ? 1.0f : opacity::disabledBody);
    }
}

juce::Path SpectrumView::line (const std::array<float, SpectrumAnalyser::kPoints>& db) const
{
    juce::Path p;
    const auto bottom = static_cast<float> (getHeight()) + 2.0f;
    for (int i = 0; i < SpectrumAnalyser::kPoints; ++i)
    {
        const auto x = xForFrequency (SpectrumAnalyser::frequencyOf (i));
        const auto y = std::min (bottom, yForDecibels (db[static_cast<std::size_t> (i)]));
        if (i == 0)
            p.startNewSubPath (x, y);
        else
            p.lineTo (x, y);
    }
    return p;
}

void SpectrumView::paintGrid (juce::Graphics& g)
{
    const auto width = static_cast<float> (getWidth());
    const auto height = static_cast<float> (getHeight());
    g.setFont (fonts::ui (type::caption));

    for (const auto hz : visualiser::gridHz)
    {
        const auto x = xForFrequency (hz);
        g.setColour (colour::gridBeat);
        g.fillRect (juce::Rectangle<float> (x, 0.0f, border::width, height));
        if (!compact)
        {
            g.setColour (colour::textSecondary);
            g.drawText (frequencyLabel (hz),
                        juce::Rectangle<float> (x + space::s1, height - size::labelHeight - space::s1,
                                                static_cast<float> (space::s6), static_cast<float> (size::labelHeight)),
                        juce::Justification::bottomLeft, false);
        }
    }

    for (const auto db : visualiser::gridDb)
    {
        const auto y = yForDecibels (db);
        g.setColour (colour::gridBeat);
        g.fillRect (juce::Rectangle<float> (0.0f, y, width, border::width));
        if (!compact)
        {
            g.setColour (colour::textSecondary);
            g.drawText (juce::String (juce::roundToInt (db)),
                        juce::Rectangle<float> (static_cast<float> (space::s1), y - size::labelHeight,
                                                static_cast<float> (space::s6), static_cast<float> (size::labelHeight)),
                        juce::Justification::bottomLeft, false);
        }
    }
}

void SpectrumView::paintSpectrum (juce::Graphics& g)
{
    const auto& spectrum = model.getSpectrum();
    const auto layer = line (spectrum.getLayerDb());

    auto area = layer;
    const auto bottom = static_cast<float> (getHeight()) + 2.0f;
    area.lineTo (static_cast<float> (getWidth()), bottom);
    area.lineTo (0.0f, bottom);
    area.closeSubPath();
    g.setColour (colour::spectrumLayer.withAlpha (visualiser::spectrumFill));
    g.fillPath (area);

    // glow.subtle as a wide, faint stroke under the line: the line changes every
    // frame, and blurring it each time would cost more than the rest together.
    const juce::PathStrokeType stroke (size::spectrumStroke, juce::PathStrokeType::beveled);
    g.setColour (colour::glowPrimary.withAlpha (glow::subtle.opacity));
    g.strokePath (layer, juce::PathStrokeType (glow::subtle.blur * 0.5f, juce::PathStrokeType::beveled));
    g.setColour (colour::spectrumLayer);
    g.strokePath (layer, stroke);

    if (showDry)
    {
        g.setColour (colour::spectrumDry);
        g.strokePath (line (spectrum.getDryDb()), stroke);
    }
}

void SpectrumView::paintCurve (juce::Graphics& g)
{
    juce::Path curve;
    const auto width = static_cast<float> (getWidth());
    for (int i = 0; i <= kCurvePoints; ++i)
    {
        const auto x = width * static_cast<float> (i) / kCurvePoints;
        const auto y = curveY (frequencyForX (x));
        if (i == 0)
            curve.startNewSubPath (x, y);
        else
            curve.lineTo (x, y);
    }

    g.setColour (colour::filterCurve.withMultipliedAlpha (toneOn ? 1.0f : opacity::disabledBody));
    g.strokePath (curve, juce::PathStrokeType (size::spectrumStroke, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
}

void SpectrumView::paintBubble (juce::Graphics& g)
{
    for (auto* handle : {highPass.get(), lowPass.get()})
    {
        if (!handle->showsValue())
            continue;

        // Above the handle, kept inside the view.
        auto bubble = juce::Rectangle<int> (size::handleBubbleWidth, size::handleBubbleHeight)
                          .withCentre (handle->getBounds().getCentre())
                          .withY (handle->getY() - size::handleBubbleHeight);
        bubble = bubble.constrainedWithin (getLocalBounds());
        if (bubble.intersects (handle->getBounds()))
            bubble.setY (handle->getBottom());

        paint::fillRaised (g, bubble.toFloat(), radius::sm, colour::bgRaised, colour::borderSubtle);
        g.setColour (colour::textPrimary);
        g.setFont (fonts::mono (type::value));
        g.drawText (handle->getValueText(), bubble, juce::Justification::centred, false);
    }
}

void SpectrumView::paint (juce::Graphics& g)
{
    // The compact curve sits in its own small recessed frame (UI.md §11.6);
    // the full view is inside the visualiser well already.
    const auto frame = getLocalBounds().toFloat();
    if (compact)
    {
        g.setColour (colour::bgDeep);
        g.fillRoundedRectangle (frame, radius::sm);
    }

    {
        const juce::Graphics::ScopedSaveState save (g);
        if (compact)
        {
            juce::Path clip;
            clip.addRoundedRectangle (frame, radius::sm);
            g.reduceClipRegion (clip);
        }
        paintGrid (g);
        paintSpectrum (g);
        paintCurve (g);
    }

    if (compact)
    {
        g.setColour (colour::borderSubtle);
        g.drawRoundedRectangle (frame.reduced (border::width * 0.5f), radius::sm, border::width);
    }
    paintBubble (g);
}

} // namespace thunder::ui
