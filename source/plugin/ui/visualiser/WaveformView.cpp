#include "WaveformView.h"

#include "plugin/ui/theme/ThunderFonts.h"
#include "plugin/ui/theme/ThunderIcons.h"
#include "plugin/ui/theme/ThunderTheme.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace thunder::ui
{

using namespace theme;

namespace
{
// A silent stretch still shows as a hairline (logical px).
constexpr float kHairline = 0.25f;

// How much of a pixel [d − ½, d + ½] lies inside [low, high].
float coverage (float d, float low, float high) noexcept
{
    return std::clamp (std::min (high - (d - 0.5f), (d + 0.5f) - low), 0.0f, 1.0f);
}

juce::uint32 toAlpha (float amount) noexcept
{
    return static_cast<juce::uint32> (juce::roundToInt (std::clamp (amount, 0.0f, 1.0f) * 255.0f));
}
} // namespace

WaveformView::WaveformView (const VisualModel& m)
    : model (m)
{
    setInterceptsMouseClicks (false, false);
    setTitle ("Waveform");
    setDescription ("The dry signal and the layer as they play, with the song's bars and beats.");
}

void WaveformView::setBars (int newBars)
{
    bars = std::clamp (newBars, 1, 4);
    repaint();
}

void WaveformView::setShowDry (bool show)
{
    showDry = show;
    repaint();
}

void WaveformView::setShowGrains (bool show)
{
    showGrains = show;
    if (!show)
        shownGrains.count = 0;
    repaint();
}

void WaveformView::setReduceMotion (bool reduce)
{
    reduceMotion = reduce;
    repaint();
}

bool WaveformView::takeGrains (double seconds)
{
    sinceGrains += seconds;
    if (!showGrains || (reduceMotion && sinceGrains < 1.0 / visualiser::reducedMotionHz))
        return false;

    sinceGrains = 0.0;
    const auto& latest = model.getGrains();
    const auto changed = std::memcmp (&latest, &shownGrains, sizeof (latest)) != 0;
    shownGrains = latest;
    return changed;
}

float WaveformView::xForAge (float ageSeconds) const noexcept
{
    // The view's own time scale; grains further back than it sit at the left
    // edge. (Before any audio the window is 120 BPM in 4/4.)
    const auto window = static_cast<float> (model.getHistory().windowSeconds (bars));
    const auto width = static_cast<float> (getWidth());
    const auto x = width * (1.0f - std::max (0.0f, ageSeconds) / std::max (window, 0.001f));
    return std::clamp (x, 0.0f, width);
}

float WaveformView::yForSemitones (float semitones) const noexcept
{
    const auto centre = static_cast<float> (getHeight()) * 0.5f;
    const auto reach = centre - size::grainSparkMax;
    const auto t = std::clamp (semitones / visualiser::grainSemitones, -1.0f, 1.0f);
    return centre - t * reach;
}

void WaveformView::scanWindow()
{
    // The frames on screen: their peaks (for the empty state and the hot
    // tint) and where the bar lines are (for their numbers).
    labels.clear();
    layerPeak = 0.0f;
    auto dryPeak = 0.0f;
    lastBarLines = 0;

    const auto& history = model.getHistory();
    if (history.getFrameSeconds() <= 0.0 || history.size() == 0)
    {
        lastPaintEmpty = true;
        return;
    }

    const auto width = static_cast<float> (getWidth());
    const auto framesInWindow = history.windowSeconds (bars) / history.getFrameSeconds();
    const auto perFrame = static_cast<float> (width / framesInWindow);
    const auto count = std::min (history.size(), static_cast<int> (std::ceil (framesInWindow)));

    for (int i = 0; i < count; ++i)
    {
        const auto& e = history.fromNewest (i);
        layerPeak = std::max ({layerPeak, std::abs (e.layerMin), std::abs (e.layerMax)});
        dryPeak = std::max ({dryPeak, std::abs (e.dryMin), std::abs (e.dryMax)});
        if (e.line == WaveformHistory::kBarLine)
        {
            labels.push_back ({width - static_cast<float> (i + 1) * perFrame, e.bar});
            ++lastBarLines;
        }
    }

    lastPaintEmpty = layerPeak < visualiser::silence && dryPeak < visualiser::silence;
}

void WaveformView::updateStrip (float scale)
{
    const auto& history = model.getHistory();
    const auto w = juce::roundToInt (static_cast<float> (getWidth()) * scale);
    const auto h = juce::roundToInt (static_cast<float> (getHeight()) * scale);
    if (w <= 0 || h <= 0 || history.getFrameSeconds() <= 0.0)
        return;

    // Columns at fixed frame boundaries: absolute column j holds frames
    // [j · fpc, (j + 1) · fpc). The rightmost is the newest complete one.
    const auto framesPerColumn = history.windowSeconds (bars) / history.getFrameSeconds() / w;
    const auto lastColumn =
        static_cast<std::int64_t> (std::floor (static_cast<double> (history.getTotalAdded()) / framesPerColumn)) - 1;

    const auto sameLayout = strip.image.isValid() && strip.image.getWidth() == w && strip.image.getHeight() == h &&
                            juce::exactlyEqual (strip.scale, scale) &&
                            std::abs (framesPerColumn - strip.framesPerColumn) <= 1.0e-9 * framesPerColumn &&
                            strip.generation == history.getGeneration() && strip.showDry == showDry &&
                            lastColumn >= strip.lastColumn;
    const auto shift = sameLayout ? lastColumn - strip.lastColumn : static_cast<std::int64_t> (w);

    if (!sameLayout)
    {
        if (!strip.image.isValid() || strip.image.getWidth() != w || strip.image.getHeight() != h)
            strip.image = juce::Image (juce::Image::ARGB, w, h, true, juce::SoftwareImageType());

        strip.scale = scale;
        strip.framesPerColumn = framesPerColumn;
        strip.generation = history.getGeneration();
        strip.showDry = showDry;

        // The halo's falloff: a Gaussian with sigma = blur / 2 (as GlowCache).
        const auto sigma = glow::medium.blur * 0.5f * scale;
        glowTable.resize (static_cast<std::size_t> (std::ceil (3.0f * sigma)) + 1);
        for (std::size_t d = 0; d < glowTable.size(); ++d)
            glowTable[d] = glow::medium.opacity * std::exp (-static_cast<float> (d * d) / (2.0f * sigma * sigma));
        ++fullRenders;
    }

    if (shift <= 0)
        return;

    strip.layerReference = std::max (layerPeak, visualiser::silence);

    if (shift >= w)
    {
        strip.dryReach = strip.layerReach = -1.0f;
        strip.lineLeft = 0;
        renderColumns (0, lastColumn - w + 1, w);
    }
    else
    {
        // Move what is there to the left; render the new columns on the right.
        const auto columns = static_cast<int> (shift);
        const juce::Image::BitmapData data (strip.image, juce::Image::BitmapData::readWrite);
        const auto bytes = static_cast<std::size_t> ((w - columns) * data.pixelStride);
        for (int y = 0; y < h; ++y)
            std::memmove (data.getLinePointer (y), data.getPixelPointer (columns, y), bytes);
        renderColumns (w - columns, strip.lastColumn + 1, columns);
    }

    strip.lastColumn = lastColumn;
}

void WaveformView::renderColumns (int firstX, std::int64_t firstColumn, int count)
{
    const auto& history = model.getHistory();
    const auto h = strip.image.getHeight();
    const auto scale = strip.scale;
    const auto fpc = strip.framesPerColumn;

    const juce::Image::BitmapData data (strip.image, firstX, 0, count, h, juce::Image::BitmapData::writeOnly);

    // Physical pixels.
    const auto centre = static_cast<float> (h) * 0.5f;
    const auto half = static_cast<float> (h) * visualiser::waveHeight;
    const auto hairline = kHairline * scale;
    const auto dryStroke = border::width * scale;
    const auto layerStroke = size::waveStroke * scale;
    const auto lineWidth = std::max (1, juce::roundToInt (border::width * scale));
    const auto hotFrom = std::min (1.0f, strip.layerReference * visualiser::layerCoreFrom) * half;

    const auto gridBar = colour::gridBar.getPixelARGB();
    const auto gridBeat = colour::gridBeat.getPixelARGB();
    const auto dryFill = colour::waveDry.getPixelARGB();
    const auto dryEdge = colour::waveDryEdge.getPixelARGB();
    const auto layerFill = colour::waveLayer.withMultipliedAlpha (visualiser::layerFill).getPixelARGB();
    const auto layerEdge = colour::waveLayer.getPixelARGB();
    const auto hot = colour::waveLayerCore.getPixelARGB();
    const auto halo = colour::glowPrimary.getPixelARGB();
    const auto rows = (h + 1) / 2;

    for (int c = 0; c < count; ++c)
    {
        const auto j = firstColumn + c;

        // The frames this column covers (at least one, when zoomed in further
        // than a frame per pixel).
        auto first = static_cast<std::int64_t> (std::ceil (static_cast<double> (j) * fpc));
        auto end = static_cast<std::int64_t> (std::ceil (static_cast<double> (j + 1) * fpc));
        if (end <= first)
        {
            first = static_cast<std::int64_t> (std::floor (static_cast<double> (j) * fpc));
            end = first + 1;
        }

        auto any = false;
        auto dryPeak = 0.0f, wavePeak = 0.0f;
        auto line = WaveformHistory::kNoLine;
        for (auto k = first; k < end; ++k)
            if (const auto* e = history.atAbsolute (k))
            {
                any = true;
                dryPeak = std::max ({dryPeak, std::abs (e->dryMin), std::abs (e->dryMax)});
                wavePeak = std::max ({wavePeak, std::abs (e->layerMin), std::abs (e->layerMax)});
                line = std::max (line, e->line);
            }

        // A grid line is a logical pixel wide: it may continue into the next columns.
        if (line != WaveformHistory::kNoLine)
        {
            strip.lineLeft = lineWidth;
            strip.lineIsBar = line == WaveformHistory::kBarLine;
        }
        const auto background =
            strip.lineLeft > 0 ? (strip.lineIsBar ? gridBar : gridBeat) : juce::PixelARGB (0, 0, 0, 0);
        strip.lineLeft = std::max (0, strip.lineLeft - 1);

        const auto dryReach = std::max (hairline, std::min (1.0f, dryPeak) * half);
        const auto layerReach = std::max (hairline, std::min (1.0f, wavePeak) * half);

        // The outline joins this column's top to its neighbour's (a stepped
        // line, smooth at one column per pixel).
        const auto band = [] (float reach, float neighbour, float stroke)
        {
            const auto from = neighbour < 0.0f ? reach : neighbour;
            return std::pair<float, float> (std::min (reach, from) - stroke * 0.5f,
                                            std::max (reach, from) + stroke * 0.5f);
        };
        const auto dryBand = band (dryReach, strip.dryReach, dryStroke);
        const auto layerBand = band (layerReach, strip.layerReach, layerStroke);

        for (int y = 0; y < rows; ++y)
        {
            auto p = background;
            if (any)
            {
                const auto d = std::abs (centre - (static_cast<float> (y) + 0.5f));

                if (showDry)
                {
                    p.blend (dryFill, toAlpha (dryReach + 0.5f - d));
                    p.blend (dryEdge, toAlpha (coverage (d, dryBand.first, dryBand.second)));
                }

                // The glow: a halo just outside the layer.
                if (const auto outside = d - layerReach; outside > 0.0f)
                {
                    const auto index = static_cast<std::size_t> (outside);
                    if (index < glowTable.size())
                        p.blend (halo, toAlpha (glowTable[index]));
                }

                const auto inside = std::clamp (layerReach + 0.5f - d, 0.0f, 1.0f);
                p.blend (layerFill, toAlpha (inside));
                if (d >= hotFrom)
                    p.blend (hot, toAlpha (inside));
                p.blend (layerEdge, toAlpha (coverage (d, layerBand.first, layerBand.second)));
            }

            *reinterpret_cast<juce::PixelARGB*> (data.getPixelPointer (c, y)) = p;
            *reinterpret_cast<juce::PixelARGB*> (data.getPixelPointer (c, h - 1 - y)) = p;
        }

        strip.dryReach = any ? dryReach : -1.0f;
        strip.layerReach = any ? layerReach : -1.0f;
    }
}

void WaveformView::paintGrid (juce::Graphics& g, float alpha)
{
    // Nothing played yet: the grid at the current (or default) tempo.
    const auto height = static_cast<float> (getHeight());
    const auto width = static_cast<float> (getWidth());
    const auto& history = model.getHistory();
    const auto beats = bars * history.getBeatsPerBar();
    for (int b = 0; b < beats; ++b)
    {
        const auto isBar = b % history.getBeatsPerBar() == 0;
        g.setColour ((isBar ? colour::gridBar : colour::gridBeat).withMultipliedAlpha (alpha));
        g.fillRect (juce::Rectangle<float> (width * static_cast<float> (b) / static_cast<float> (beats), 0.0f,
                                            border::width, height));
    }
}

void WaveformView::paintLabels (juce::Graphics& g, float alpha)
{
    g.setFont (fonts::ui (type::caption));
    g.setColour (colour::textSecondary.withMultipliedAlpha (alpha));
    for (const auto& label : labels)
        g.drawText (juce::String (label.bar),
                    juce::Rectangle<float> (label.x + space::s1, static_cast<float> (space::s1),
                                            static_cast<float> (space::s7), static_cast<float> (size::labelHeight)),
                    juce::Justification::topLeft, false);
}

void WaveformView::paintGrains (juce::Graphics& g)
{
    for (int i = 0; i < shownGrains.count; ++i)
    {
        const auto& spark = shownGrains.sparks[static_cast<std::size_t> (i)];
        const auto x = xForAge (spark.ageSeconds);
        const auto y = yForSemitones (spark.semitones);
        const auto window = std::clamp (spark.window, 0.0f, 1.0f);
        const auto tint = colour::waveLayer.interpolatedWith (colour::accentSecondary, std::abs (spark.pan));

        if (!reduceMotion)
        {
            g.setColour (tint.withAlpha (visualiser::grainTrail));
            g.drawLine (xForAge (spark.startAgeSeconds), y, x, y, border::width);
        }

        // Size and brightness follow the window (largest mid-grain).
        const auto d = size::grainSparkMin + (size::grainSparkMax - size::grainSparkMin) * window;
        const auto brightness = 0.35f + 0.65f * window;
        g.setColour (tint.withAlpha (brightness));
        g.fillEllipse (x - d * 0.5f, y - d * 0.5f, d, d);
        g.setColour (colour::accentHot.withAlpha (brightness));
        g.fillEllipse (x - d * 0.225f, y - d * 0.225f, d * 0.45f, d * 0.45f);
    }
}

void WaveformView::paintEmpty (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const auto bolt = static_cast<float> (size::emptyBolt);
    const auto glyph = juce::Rectangle<float> (bolt, bolt).withCentre (area.getCentre().translated (0.0f, -space::s3));
    g.setColour (colour::accentPrimary.withAlpha (opacity::placeholder));
    g.fillPath (icons::fitted (icons::bolt(), glyph));

    g.setColour (colour::textSecondary);
    g.setFont (fonts::ui (type::body));
    g.drawText ("Play your melody to see the layer",
                area.withTop (glyph.getBottom() + space::s2).withHeight (static_cast<float> (size::labelHeight + 4)),
                juce::Justification::centredTop, false);
}

void WaveformView::paint (juce::Graphics& g)
{
    if (getWidth() <= 0 || getHeight() <= 0)
        return;

    scanWindow();

    if (model.getHistory().size() == 0)
    {
        paintGrid (g, visualiser::dimmed);
        paintEmpty (g);
        return;
    }

    // The strip at one pixel per physical pixel, drawn 1:1.
    const auto scale = std::max (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    updateStrip (scale);

    const auto stopped = model.getSecondsStopped() > visualiser::dimAfterSeconds;
    const auto alpha = stopped || lastPaintEmpty ? visualiser::dimmed : 1.0f;
    {
        const juce::Graphics::ScopedSaveState save (g);
        g.setOpacity (alpha);
        g.drawImageTransformed (strip.image, juce::AffineTransform::scale (1.0f / strip.scale));
    }

    paintLabels (g, alpha);
    if (showGrains)
        paintGrains (g);
    if (lastPaintEmpty)
        paintEmpty (g);
}

} // namespace thunder::ui
