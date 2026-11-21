#include "ThunderPaint.h"

#include <algorithm>
#include <cmath>

namespace thunder::ui::paint
{

using namespace theme;

// ---- GlowCache
// -------------------------------------------------------------------------------------------------------

void GlowCache::draw (juce::Graphics& g, const juce::Path& shape, juce::Colour colour, const Glow& glow, float opacity)
{
    if (opacity <= 0.0f || shape.isEmpty())
        return;

    const auto scale = std::max (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());

    if (image.isNull() || shape != cachedShape || !juce::exactlyEqual (scale, cachedScale) ||
        !juce::exactlyEqual (glow.blur, cachedBlur))
    {
        // The blur spreads about `blur` px; leave room for all of it.
        area = shape.getBounds().expanded (glow.blur + 2.0f);
        const auto w = std::max (1, juce::roundToInt (std::ceil (area.getWidth() * scale)));
        const auto h = std::max (1, juce::roundToInt (std::ceil (area.getHeight() * scale)));

        image = juce::Image (juce::Image::SingleChannel, w, h, true, juce::SoftwareImageType());
        {
            juce::Graphics ig (image);
            ig.setColour (juce::Colours::white);
            ig.fillPath (shape, juce::AffineTransform::translation (-area.getX(), -area.getY()).scaled (scale));
        }

        // Three box blurs of radius r approximate a Gaussian with sigma ≈ r; a
        // CSS-style blur radius B has sigma B / 2.
        const auto radiusPx = std::max (1, juce::roundToInt (glow.blur * 0.5f * scale));
        image.getPixelData()->applySingleChannelBoxBlurEffect (radiusPx);

        cachedShape = shape;
        cachedScale = scale;
        cachedBlur = glow.blur;
        ++renders;
    }

    const juce::Graphics::ScopedSaveState save (g);
    g.setColour (colour.withMultipliedAlpha (juce::jlimit (0.0f, 1.0f, opacity)));
    g.drawImageTransformed (image, juce::AffineTransform::scale (1.0f / scale).translated (area.getX(), area.getY()),
                            true);
}

// ---- Flash
// -----------------------------------------------------------------------------------------------------------

Flash::Flash (juce::Component& c)
    : owner (c)
{
}

Flash::~Flash()
{
    stopTimer();
}

void Flash::trigger()
{
    startMs = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (motion::frameHz);
    owner.repaint();
}

float Flash::getLevel() const noexcept
{
    if (!isTimerRunning())
        return 0.0f;

    const auto t = juce::jlimit (0.0, 1.0, (juce::Time::getMillisecondCounterHiRes() - startMs) / motion::base);
    const auto remaining = 1.0 - t;
    return static_cast<float> (remaining * remaining); // ease-out
}

void Flash::timerCallback()
{
    if (juce::Time::getMillisecondCounterHiRes() - startMs >= motion::base)
        stopTimer();
    owner.repaint();
}

// ---- Shapes
// ----------------------------------------------------------------------------------------------------------

void drawFocusRing (juce::Graphics& g, juce::Rectangle<float> bounds, float cornerRadius)
{
    const auto ring = bounds.expanded (border::focusOffset);
    const auto r = std::min (cornerRadius + border::focusOffset, ring.getHeight() * 0.5f);

    g.setColour (colour::glowPrimary.withAlpha (glow::subtle.opacity));
    g.drawRoundedRectangle (ring.expanded (border::width), r + border::width, border::width * 2.0f);
    g.setColour (colour::borderStrong);
    g.drawRoundedRectangle (ring, r, border::width);
}

void fillRaised (juce::Graphics& g, juce::Rectangle<float> bounds, float cornerRadius, juce::Colour fill,
                 juce::Colour border)
{
    const auto r = std::min (cornerRadius, bounds.getHeight() * 0.5f);
    g.setColour (fill);
    g.fillRoundedRectangle (bounds, r);

    // The inner top highlight: a 1 px line just inside the top edge.
    g.setColour (colour::highlight.withAlpha (opacity::topHighlight));
    g.drawHorizontalLine (juce::roundToInt (bounds.getY() + border::width), bounds.getX() + r, bounds.getRight() - r);

    g.setColour (border);
    g.drawRoundedRectangle (bounds.reduced (border::width * 0.5f), r, border::width);
}

void drawShadow (juce::Graphics& g, juce::Rectangle<int> bounds, const Shadow& shadow)
{
    juce::DropShadow (colour::shadow.withAlpha (shadow.opacity), shadow.blur / 2, {0, shadow.offsetY})
        .drawForRectangle (g, bounds);
}

void CachedLayer::draw (juce::Graphics& g, juce::Rectangle<int> bounds,
                        const std::function<void (juce::Graphics&)>& painter)
{
    const auto scale = std::max (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());

    if (image.isNull() || bounds != cachedBounds || !juce::exactlyEqual (scale, cachedScale))
    {
        image = juce::Image (juce::Image::ARGB,
                             std::max (1, juce::roundToInt (static_cast<float> (bounds.getWidth()) * scale)),
                             std::max (1, juce::roundToInt (static_cast<float> (bounds.getHeight()) * scale)), true,
                             juce::SoftwareImageType());
        {
            juce::Graphics ig (image);
            ig.addTransform (juce::AffineTransform::translation (static_cast<float> (-bounds.getX()),
                                                                 static_cast<float> (-bounds.getY()))
                                 .scaled (scale));
            painter (ig);
        }
        cachedBounds = bounds;
        cachedScale = scale;
        ++renders;
    }

    g.drawImageTransformed (image,
                            juce::AffineTransform::scale (1.0f / scale).translated (bounds.getPosition().toFloat()));
}

const juce::Image& grainTile()
{
    static const juce::Image tile = []
    {
        constexpr int tileSize = 128;
        juce::Image image (juce::Image::ARGB, tileSize, tileSize, true, juce::SoftwareImageType());
        juce::Random random (static_cast<juce::int64> (texture::grainSeed));
        for (int y = 0; y < tileSize; ++y)
            for (int x = 0; x < tileSize; ++x)
            {
                // Light or dark specks of random strength, at most grainOpacity.
                const auto strength = random.nextFloat() * texture::grainOpacity;
                const auto c = random.nextBool() ? colour::highlight : colour::shadow;
                image.setPixelAt (x, y, c.withAlpha (strength));
            }
        return image;
    }();
    return tile;
}

void paintBackground (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    g.setColour (colour::bgBase);
    g.fillRect (bounds);

    // Soft vignette: edges about 15% darker.
    const auto centre = juce::Point<float> (bounds.getCentreX(), bounds.getY() + bounds.getHeight() * 0.45f);
    juce::ColourGradient vignette (colour::shadow.withAlpha (0.0f), centre,
                                   colour::shadow.withAlpha (texture::vignetteOpacity), bounds.getTopLeft(), true);
    vignette.addColour (0.55, colour::shadow.withAlpha (0.0f));
    g.setGradientFill (vignette);
    g.fillRect (bounds);

    g.setTiledImageFill (grainTile(), 0, 0, 1.0f);
    g.fillRect (bounds);
}

} // namespace thunder::ui::paint
