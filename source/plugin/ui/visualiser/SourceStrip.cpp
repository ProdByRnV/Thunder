#include "SourceStrip.h"

#include "plugin/ui/components/KeyBadge.h"
#include "plugin/ui/theme/ThunderFonts.h"
#include "plugin/ui/theme/ThunderTheme.h"

namespace thunder::ui
{

using namespace theme;
using Phase = analysis::AnalysisWorker::SourcePhase;

SourceStrip::SourceStrip()
    : removeButton ("Remove the source", icons::close())
{
    setTitle ("Source");
    removeButton.onClick = [this]
    {
        if (onRemove)
            onRemove();
    };
    addAndMakeVisible (removeButton);
    setVisible (false);
}

void SourceStrip::setStatus (const analysis::AnalysisWorker::SourceStatus& newStatus)
{
    status = newStatus;
    setVisible (status.phase != Phase::None);
    setDescription (getCaption());
    repaint();
}

void SourceStrip::setHostTempo (double bpm, int numerator, int denominator)
{
    if (bpm <= 0.0 || numerator <= 0 || denominator <= 0)
        return;
    if (juce::exactlyEqual (bpm, hostBpm) && numerator == hostNumerator && denominator == hostDenominator)
        return;
    hostBpm = bpm;
    hostNumerator = numerator;
    hostDenominator = denominator;
    if (status.buffer != nullptr && status.buffer->bpm <= 0.0)
        repaint (waveArea);
}

double SourceStrip::barSeconds() const noexcept
{
    const auto* b = status.buffer.get();
    const auto known = b != nullptr && b->bpm > 0.0;
    const auto bpm = known ? b->bpm : hostBpm;
    const auto quarters =
        known ? b->timeSigNumerator * 4.0 / b->timeSigDenominator : hostNumerator * 4.0 / hostDenominator;
    return quarters * 60.0 / bpm;
}

juce::String SourceStrip::getCaption() const
{
    const auto dot = juce::String::fromUTF8 (" \xc2\xb7 ");
    switch (status.phase)
    {
        case Phase::None:
            return {};
        case Phase::Loading:
            return status.name + dot + "Loading " + juce::String (juce::roundToInt (status.progress * 100.0f)) + "%";
        case Phase::Failed:
            return status.name + dot + source::describe (status.error);
        case Phase::Ready:
            break;
    }

    const auto& b = *status.buffer;
    auto text = status.name + dot +
                (b.fromCapture && b.bars > 0 ? juce::String (b.bars) + (b.bars == 1 ? " bar" : " bars")
                                             : juce::String (b.getSeconds(), 1) + " s");
    if (b.key.valid)
        text += dot + KeyBadge::keyName (b.key);
    return text;
}

std::vector<float> SourceStrip::barLinePositions() const
{
    std::vector<float> lines;
    if (status.buffer == nullptr || waveArea.isEmpty())
        return lines;

    const auto seconds = status.buffer->getSeconds();
    const auto bar = barSeconds();
    if (seconds <= 0.0 || bar <= 0.0)
        return lines;

    const auto offset = static_cast<double> (status.buffer->gridOffsetSamples) / status.buffer->sampleRate;
    // At most one line every 4 px: otherwise every fourth bar.
    const auto step = bar * (waveArea.getWidth() * bar / seconds < 4.0 ? 4.0 : 1.0);
    for (auto t = offset; t < seconds; t += step)
        lines.push_back (static_cast<float> (waveArea.getX()) +
                         static_cast<float> (t / seconds) * static_cast<float> (waveArea.getWidth()));
    return lines;
}

void SourceStrip::resized()
{
    auto area = getLocalBounds().reduced (space::s3, 0);
    removeButton.setBounds (
        area.removeFromRight (size::iconButton).withSizeKeepingCentre (size::iconButton, size::iconButton));
    area.removeFromRight (space::s2);
    labelArea = area.removeFromLeft (space::s7 + space::s2);
    nameArea = area.removeFromLeft (visualiser::sourceNameWidth);
    keyArea = area.removeFromRight (visualiser::sourceKeyWidth);
    area.removeFromRight (space::s2);
    waveArea = area.reduced (0, space::s2);
}

void SourceStrip::paint (juce::Graphics& g)
{
    g.setColour (colour::bgPanel.withAlpha (visualiser::sourceStripOpacity));
    g.fillRect (getLocalBounds());
    g.setColour (colour::borderSubtle);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, static_cast<float> (getWidth()));

    g.setColour (colour::textSecondary);
    g.setFont (fonts::ui (type::label, fonts::Weight::semiBold).withExtraKerningFactor (type::labelTracking));
    g.drawText ("SOURCE", labelArea, juce::Justification::centredLeft);

    g.setColour (status.phase == Phase::Failed ? colour::stateError : colour::textPrimary);
    g.setFont (fonts::ui (type::body));
    g.drawFittedText (status.name, nameArea.reduced (space::s1, 0), juce::Justification::centredLeft, 1);

    const auto wave = waveArea.toFloat();
    if (status.phase == Phase::Loading)
    {
        // Progress, like the capture chip's.
        const auto track = wave.withSizeKeepingCentre (wave.getWidth(), static_cast<float> (space::s1));
        g.setColour (colour::meterTrack);
        g.fillRoundedRectangle (track, radius::sm);
        g.setColour (colour::accentPrimaryDim);
        g.fillRoundedRectangle (track.withWidth (track.getWidth() * status.progress), radius::sm);
        g.setColour (colour::textSecondary);
        g.setFont (fonts::ui (type::caption));
        g.drawText ("Loading", keyArea, juce::Justification::centredRight);
        return;
    }

    if (status.phase == Phase::Failed)
    {
        g.setColour (colour::stateError);
        g.setFont (fonts::ui (type::caption));
        g.drawFittedText (source::describe (status.error), waveArea.withRight (keyArea.getRight()),
                          juce::Justification::centredLeft, 2);
        return;
    }

    if (status.buffer == nullptr)
        return;

    // The waveform: one bar per pixel column, mirrored about the centre.
    const auto& peaks = status.buffer->peaks;
    if (!peaks.empty() && wave.getWidth() > 0.0f)
    {
        juce::Path shape;
        const auto columns = static_cast<int> (wave.getWidth());
        const auto mid = wave.getCentreY();
        const auto half = wave.getHeight() * 0.5f;
        for (int x = 0; x < columns; ++x)
        {
            const auto from = static_cast<std::size_t> (x) * peaks.size() / static_cast<std::size_t> (columns);
            const auto to = std::max (from + 1, static_cast<std::size_t> (x + 1) * peaks.size() /
                                                    static_cast<std::size_t> (columns));
            float peak = 0.0f;
            for (auto i = from; i < std::min (to, peaks.size()); ++i)
                peak = std::max (peak, peaks[i]);
            const auto h = std::max (0.5f, std::min (1.0f, peak) * half);
            shape.addRectangle (wave.getX() + static_cast<float> (x), mid - h, 1.0f, 2.0f * h);
        }
        g.setColour (colour::waveDryEdge);
        g.fillPath (shape);
    }

    g.setColour (colour::borderStrong);
    for (const auto x : barLinePositions())
        g.drawVerticalLine (juce::roundToInt (x), wave.getY(), wave.getBottom());

    if (const auto& key = status.buffer->key; key.valid)
    {
        g.setColour (colour::textSecondary);
        g.setFont (fonts::ui (type::caption));
        g.drawText (KeyBadge::keyName (key) + " " + juce::String (juce::roundToInt (key.confidence * 100.0f)) + "%",
                    keyArea, juce::Justification::centredRight);
    }
}

} // namespace thunder::ui
