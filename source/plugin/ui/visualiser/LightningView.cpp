#include "LightningView.h"

#include "core/util/Rng.h"
#include "plugin/ui/Motion.h"
#include "plugin/ui/theme/ThunderTheme.h"

#include <algorithm>

namespace thunder::ui
{

namespace colour = theme::colour;
namespace glow = theme::glow;
namespace space = theme::space;
namespace tokens = theme::lightning;

struct LightningView::Bolt
{
    lightning::BoltShape shape;
    std::vector<juce::Path> paths; // one per strand: the line

    // The whole bolt at full brightness (glow, body and core), rendered once
    // at the display's pixel scale on whole pixels; each frame draws it at
    // the strike's level. Every pass scales with the level, so this is exact.
    juce::Image sprite;
    juce::Rectangle<int> spriteArea;
    float spriteScale = 0.0f;
    double age = 0.0;
    bool flicker = false;
    float brightness = 1.0f;

    float level() const noexcept { return lightning::strikeLevel (age, flicker) * brightness; }
    bool isOver() const noexcept { return age >= lightning::strikeLength (flicker); }
};

LightningView::LightningView()
{
    setInterceptsMouseClicks (false, false);
    setWantsKeyboardFocus (false);
    setAccessible (false); // decoration: the views under it carry the information
}

LightningView::~LightningView() = default;

void LightningView::strike (float peak, float brightness)
{
    if (getWidth() <= 0 || getHeight() <= 0 || brightness <= 0.0f || ui::reduceMotion::isOn())
        return;

    // UI.md §12.5: a strike that would make a 4th flash in a second is dropped.
    if (!flashes.tryAdd (clock))
    {
        ++dropped;
        return;
    }

    ++strikes;
    util::Rng rng (strikes, 0x7b01u);
    const auto w = static_cast<float> (getWidth());
    const auto h = static_cast<float> (getHeight());

    // Towards the layer's peak where the newest audio is drawn (the right).
    const auto centre = h * 0.5f;
    const auto reach = h * theme::visualiser::waveHeight * std::clamp (peak, 0.15f, 1.0f);
    const juce::Point<float> end (w - static_cast<float> (space::s5) - rng.nextFloat() * static_cast<float> (space::s6),
                                  centre - reach);
    // From the top edge, up to a third of the well's width to the left: bolts
    // come down more than across.
    const juce::Point<float> start (end.x - w * tokens::maxLean * rng.nextFloat(), 0.0f);

    // Inside the well, clear of the view toggle's corner.
    auto within = getLocalBounds().toFloat().reduced (tokens::glowWidth * 0.5f);
    if (!keepClear.isEmpty())
        within = within.withBottom (std::min (within.getBottom(), static_cast<float> (keepClear.getY())));

    auto bolt = std::make_unique<Bolt>();
    bolt->shape = lightning::generateBolt (start, end, strikes, within);
    // The re-flicker is a flash too: only if it fits the limit.
    bolt->flicker = rng.nextFloat() < 0.5f && flashes.tryAdd (clock + tokens::hold + tokens::flickerGap);
    bolt->brightness = std::clamp (brightness, 0.0f, 1.0f);

    for (const auto& strand : bolt->shape.strands)
    {
        juce::Path p;
        for (std::size_t i = 0; i < strand.points.size(); ++i)
        {
            if (i == 0)
                p.startNewSubPath (strand.points[i]);
            else
                p.lineTo (strand.points[i]);
        }
        bolt->paths.push_back (std::move (p));
    }

    if (static_cast<int> (bolts.size()) >= tokens::maxBolts)
    {
        repaint (dirtyArea());
        bolts.erase (bolts.begin());
    }
    bolts.push_back (std::move (bolt));

    ambientAge = 0.0;
    ambientBrightness = brightness;
    repaint();
}

const lightning::BoltShape& LightningView::getBolt (int index) const
{
    return bolts[static_cast<std::size_t> (index)]->shape;
}

float LightningView::getAmbient() const noexcept
{
    return lightning::ambientLevel (ambientAge) * ambientBrightness;
}

juce::Rectangle<int> LightningView::dirtyArea() const
{
    if (getAmbient() > 0.0f)
        return getLocalBounds();

    juce::Rectangle<float> area;
    for (const auto& b : bolts)
        area = area.isEmpty() ? b->shape.getBounds() : area.getUnion (b->shape.getBounds());
    const auto margin = glow::strong.blur + tokens::glowWidth;
    return area.expanded (margin).getSmallestIntegerContainer().getIntersection (getLocalBounds());
}

void LightningView::clear()
{
    if (!bolts.empty() || getAmbient() > 0.0f)
        repaint();
    bolts.clear();
    ambientAge = 1.0e9;
}

bool LightningView::advance (double seconds)
{
    clock += seconds;
    if (bolts.empty() && getAmbient() <= 0.0f)
        return false;

    // What was drawn (so ending strikes are cleared), then what is drawn now.
    auto dirty = dirtyArea();
    for (auto& b : bolts)
        b->age += seconds;
    ambientAge += seconds;
    bolts.erase (std::remove_if (bolts.begin(), bolts.end(), [] (const auto& b) { return b->isOver(); }), bolts.end());
    dirty = dirty.getUnion (dirtyArea());

    if (!dirty.isEmpty())
        repaint (dirty);
    return true;
}

void LightningView::paint (juce::Graphics& g)
{
    // The ambient flash brightens the well only (UI.md §12.5: never the window).
    if (const auto ambient = getAmbient(); ambient > 0.0f)
    {
        g.setColour (colour::highlight.withAlpha (tokens::ambientLevel * ambient));
        g.fillRect (getLocalBounds());
    }

    const auto scale = std::max (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    for (auto& b : bolts)
    {
        const auto level = b->level();
        if (level <= 0.0f)
            continue;

        if (b->sprite.isNull() || !juce::exactlyEqual (b->spriteScale, scale))
            renderSprite (*b, scale);

        g.setOpacity (std::min (1.0f, level));
        g.drawImageTransformed (b->sprite, juce::AffineTransform::scale (1.0f / scale)
                                               .translated (static_cast<float> (b->spriteArea.getX()),
                                                            static_cast<float> (b->spriteArea.getY())));
    }
}

void LightningView::renderSprite (Bolt& b, float scale)
{
    b.spriteArea = b.shape.getBounds().expanded (glow::strong.blur + tokens::glowWidth).getSmallestIntegerContainer();
    const auto w = std::max (1, juce::roundToInt (static_cast<float> (b.spriteArea.getWidth()) * scale));
    const auto h = std::max (1, juce::roundToInt (static_cast<float> (b.spriteArea.getHeight()) * scale));
    const auto origin = b.spriteArea.getPosition().toFloat();
    const auto toSprite = juce::AffineTransform::translation (-origin.x, -origin.y).scaled (scale);
    const auto stroke = [] (float width)
    { return juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded); };

    // 1. The glow: every strand stroked glowWidth wide into a small
    // single-channel image at glowResolution of the pixels, box-blurred (three
    // passes approximate a Gaussian; a CSS-style radius B has sigma B / 2).
    const auto small = scale * tokens::glowResolution;
    juce::Image glowMask (juce::Image::SingleChannel,
                          std::max (1, juce::roundToInt (static_cast<float> (w) * tokens::glowResolution)),
                          std::max (1, juce::roundToInt (static_cast<float> (h) * tokens::glowResolution)), true,
                          juce::SoftwareImageType());
    {
        juce::Graphics mg (glowMask);
        mg.setColour (juce::Colours::white);
        const auto toMask = juce::AffineTransform::translation (-origin.x, -origin.y).scaled (small);
        for (const auto& p : b.paths)
            mg.strokePath (p, stroke (tokens::glowWidth), toMask);
    }
    glowMask.getPixelData()->applySingleChannelBoxBlurEffect (
        std::max (1, juce::roundToInt (glow::strong.blur * 0.5f * small)));

    b.sprite = juce::Image (juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
    {
        juce::Graphics sg (b.sprite);
        sg.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        sg.setColour (colour::glowPrimary.withAlpha (tokens::glowOpacity));
        sg.drawImageTransformed (glowMask, juce::AffineTransform::scale (1.0f / tokens::glowResolution), true);

        // 2. The body and 3. the hot core, each strand at its brightness.
        for (std::size_t i = 0; i < b.paths.size(); ++i)
        {
            const auto strandLevel = b.shape.strands[i].brightness;
            sg.setColour (colour::accentPrimary.withAlpha (tokens::bodyOpacity * strandLevel));
            sg.strokePath (b.paths[i], stroke (tokens::bodyWidth), toSprite);
            sg.setColour (colour::accentHot.withAlpha (strandLevel));
            sg.strokePath (b.paths[i], stroke (tokens::coreWidth), toSprite);
        }
    }
    b.spriteScale = scale;
}

} // namespace thunder::ui
