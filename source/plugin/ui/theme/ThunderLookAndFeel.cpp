#include "ThunderLookAndFeel.h"

#include "ThunderFonts.h"
#include "ThunderPaint.h"
#include "ThunderTheme.h"

namespace thunder::ui
{

using namespace theme;

ThunderLookAndFeel::ThunderLookAndFeel()
{
    setColourScheme ({colour::bgBase, colour::bgPanel, colour::bgRaised, colour::borderSubtle, colour::textPrimary,
                      colour::accentPrimary, colour::textOnAccent, colour::bgHover, colour::textPrimary});

    const std::pair<int, juce::Colour> colourTable[] = {
        {juce::ResizableWindow::backgroundColourId, colour::bgBase},
        {juce::Label::textColourId, colour::textPrimary},
        {juce::TextButton::buttonColourId, colour::bgRaised},
        {juce::TextButton::buttonOnColourId, colour::accentPrimary},
        {juce::TextButton::textColourOffId, colour::textPrimary},
        {juce::TextButton::textColourOnId, colour::textOnAccent},
        {juce::ToggleButton::textColourId, colour::textSecondary},
        {juce::ToggleButton::tickColourId, colour::textOnAccent},
        {juce::ComboBox::backgroundColourId, colour::bgRaised},
        {juce::ComboBox::textColourId, colour::textPrimary},
        {juce::ComboBox::outlineColourId, colour::borderSubtle},
        {juce::ComboBox::arrowColourId, colour::textSecondary},
        {juce::ComboBox::focusedOutlineColourId, colour::borderStrong},
        {juce::PopupMenu::backgroundColourId, colour::bgRaised},
        {juce::PopupMenu::textColourId, colour::textPrimary},
        {juce::PopupMenu::headerTextColourId, colour::textSecondary},
        {juce::PopupMenu::highlightedBackgroundColourId, colour::bgHover},
        {juce::PopupMenu::highlightedTextColourId, colour::textPrimary},
        {juce::TextEditor::backgroundColourId, colour::bgRaised},
        {juce::TextEditor::textColourId, colour::textPrimary},
        {juce::TextEditor::highlightColourId, colour::accentPrimaryDim},
        {juce::TextEditor::highlightedTextColourId, colour::textPrimary},
        {juce::TextEditor::outlineColourId, colour::borderSubtle},
        {juce::TextEditor::focusedOutlineColourId, colour::borderStrong},
        {juce::CaretComponent::caretColourId, colour::accentPrimary},
        {juce::TooltipWindow::backgroundColourId, colour::bgRaised},
        {juce::TooltipWindow::textColourId, colour::textPrimary},
        {juce::TooltipWindow::outlineColourId, colour::borderSubtle},
        {juce::Slider::rotarySliderFillColourId, colour::accentPrimary},
        {juce::Slider::rotarySliderOutlineColourId, colour::borderSubtle},
        {juce::Slider::thumbColourId, colour::textPrimary},
        {juce::Slider::textBoxTextColourId, colour::textPrimary},
        {juce::Slider::textBoxOutlineColourId, colour::borderSubtle},
        {juce::AlertWindow::backgroundColourId, colour::bgPanel},
        {juce::AlertWindow::textColourId, colour::textPrimary},
        {juce::AlertWindow::outlineColourId, colour::borderSubtle},
        {juce::ScrollBar::thumbColourId, colour::borderStrong},
    };

    for (const auto& [id, c] : colourTable)
        setColour (id, c);
}

void ThunderLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool)
{
    // C-CHECKBOX (UI.md §9.8): a 14 px box, then the label; the whole row clicks.
    const auto boxSize = static_cast<float> (size::checkboxBox);
    const auto box = juce::Rectangle<float> (boxSize, boxSize)
                         .withPosition (border::focusOffset, (static_cast<float> (button.getHeight()) - boxSize) * 0.5f)
                         .reduced (border::width * 0.5f);

    if (button.getToggleState())
    {
        g.setColour (button.isEnabled() ? colour::accentPrimary : colour::textDisabled);
        g.fillRoundedRectangle (box, radius::sm);

        juce::Path tick;
        tick.startNewSubPath (box.getX() + box.getWidth() * 0.22f, box.getCentreY());
        tick.lineTo (box.getX() + box.getWidth() * 0.43f, box.getBottom() - box.getHeight() * 0.25f);
        tick.lineTo (box.getRight() - box.getWidth() * 0.2f, box.getY() + box.getHeight() * 0.25f);
        g.setColour (colour::textOnAccent);
        g.strokePath (tick, juce::PathStrokeType (size::knobPointer));
    }
    else
    {
        g.setColour (highlighted ? colour::bgHover : colour::bgRaised);
        g.fillRoundedRectangle (box, radius::sm);
        g.setColour (colour::borderStrong);
        g.drawRoundedRectangle (box, radius::sm, border::width);
    }

    if (button.hasKeyboardFocus (false))
        paint::drawFocusRing (g, box, radius::sm);

    g.setColour (button.isEnabled() ? (highlighted ? colour::textPrimary : colour::textSecondary)
                                    : colour::textDisabled);
    g.setFont (fonts::ui (type::label, fonts::Weight::medium).withExtraKerningFactor (type::labelTracking));
    g.drawFittedText (button.getButtonText(),
                      button.getLocalBounds().withTrimmedLeft (size::checkboxBox + space::s2 +
                                                               static_cast<int> (border::focusOffset)),
                      juce::Justification::centredLeft, 1);
}

void ThunderLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& background,
                                               bool highlighted, bool down)
{
    // C-BUTTON, secondary (UI.md §9.6), for any plain JUCE button.
    const auto bounds = button.getLocalBounds().toFloat().reduced (border::width * 0.5f);
    auto fill = button.getToggleState() ? colour::accentPrimary : background;
    if (!button.getToggleState() && (highlighted || down))
        fill = colour::bgHover;

    paint::fillRaised (g, bounds, radius::sm, fill, colour::borderSubtle);
    if (button.hasKeyboardFocus (false))
        paint::drawFocusRing (g, bounds, radius::sm);
}

juce::Font ThunderLookAndFeel::getTextButtonFont (juce::TextButton&, int)
{
    return fonts::ui (type::body, fonts::Weight::medium);
}

void ThunderLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int,
                                       juce::ComboBox& box)
{
    // C-DROPDOWN (UI.md §9.5).
    const auto bounds = juce::Rectangle<int> (width, height).toFloat().reduced (border::width * 0.5f);
    paint::fillRaised (g, bounds, radius::sm,
                       box.isMouseOver (true) && box.isEnabled() ? colour::bgHover : colour::bgRaised,
                       box.isPopupActive() ? colour::borderStrong : colour::borderSubtle);
    if (box.hasKeyboardFocus (true))
        paint::drawFocusRing (g, bounds, radius::sm);

    const auto chevronArea = juce::Rectangle<float> (static_cast<float> (height), static_cast<float> (height))
                                 .withRightX (bounds.getRight())
                                 .reduced (static_cast<float> (height) * 0.36f);
    juce::Path chevron;
    chevron.startNewSubPath (chevronArea.getX(), chevronArea.getY() + chevronArea.getHeight() * 0.3f);
    chevron.lineTo (chevronArea.getCentreX(), chevronArea.getBottom() - chevronArea.getHeight() * 0.2f);
    chevron.lineTo (chevronArea.getRight(), chevronArea.getY() + chevronArea.getHeight() * 0.3f);
    g.setColour (box.isEnabled() ? colour::textSecondary : colour::textDisabled);
    g.strokePath (chevron,
                  juce::PathStrokeType (size::iconStroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font ThunderLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return fonts::ui (type::body);
}

void ThunderLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (space::s2, 0, box.getWidth() - box.getHeight() - space::s1, box.getHeight());
    label.setFont (getComboBoxFont (box));
}

// ---- Menus (UI.md §9.5: elevation 3, the selected item marked with a cyan bar)
// ---------------------------------------------------------------

juce::Font ThunderLookAndFeel::getPopupMenuFont()
{
    return fonts::ui (type::body);
}

void ThunderLookAndFeel::drawPopupMenuBackgroundWithOptions (juce::Graphics& g, int width, int height,
                                                             const juce::PopupMenu::Options&)
{
    const auto bounds = juce::Rectangle<int> (width, height).toFloat();
    g.fillAll (colour::bgRaised);
    g.setColour (colour::borderSubtle);
    g.drawRect (bounds, border::width);
}

int ThunderLookAndFeel::getPopupMenuBorderSizeWithOptions (const juce::PopupMenu::Options&)
{
    return space::s1;
}

void ThunderLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator,
                                            bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                                            const juce::String& text, const juce::String& shortcutKeyText,
                                            const juce::Drawable*, const juce::Colour* textColour)
{
    if (isSeparator)
    {
        g.setColour (colour::borderSubtle);
        g.fillRect (area.withSizeKeepingCentre (area.getWidth(), 1));
        return;
    }

    if (isHighlighted && isActive)
    {
        g.setColour (colour::bgHover);
        g.fillRect (area);
    }

    // The selected item: a 2 px accent bar on the left (not colour alone: it also reads as a mark).
    if (isTicked)
    {
        g.setColour (colour::accentPrimary);
        g.fillRect (area.toFloat().withWidth (size::menuSelectedBar));
    }

    auto r = area.reduced (space::s3, 0);
    g.setColour (!isActive ? colour::textDisabled : (textColour != nullptr ? *textColour : colour::textPrimary));
    g.setFont (isTicked ? fonts::ui (type::body, fonts::Weight::semiBold) : fonts::ui (type::body));

    if (hasSubMenu)
    {
        const auto arrow = r.removeFromRight (space::s2).toFloat().withSizeKeepingCentre (
            static_cast<float> (space::s2), static_cast<float> (space::s2));
        juce::Path p;
        p.startNewSubPath (arrow.getX(), arrow.getY());
        p.lineTo (arrow.getRight(), arrow.getCentreY());
        p.lineTo (arrow.getX(), arrow.getBottom());
        g.strokePath (p, juce::PathStrokeType (size::iconStroke));
    }

    if (shortcutKeyText.isNotEmpty())
    {
        g.setColour (colour::textSecondary);
        g.setFont (fonts::ui (type::caption));
        g.drawText (shortcutKeyText, r, juce::Justification::centredRight);
        g.setColour (!isActive ? colour::textDisabled : colour::textPrimary);
        g.setFont (fonts::ui (type::body));
    }

    g.drawFittedText (text, r, juce::Justification::centredLeft, 1);
}

void ThunderLookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area,
                                                     const juce::String& sectionName)
{
    g.setColour (colour::textSecondary);
    g.setFont (fonts::ui (type::label, fonts::Weight::semiBold).withExtraKerningFactor (type::labelTracking));
    g.drawText (sectionName.toUpperCase(), area.reduced (space::s3, 0), juce::Justification::bottomLeft);
}

void ThunderLookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int, int& idealWidth,
                                                    int& idealHeight)
{
    if (isSeparator)
    {
        idealWidth = space::s6;
        idealHeight = space::s2 + 1;
        return;
    }

    idealHeight = size::controlHeight;
    idealWidth = juce::GlyphArrangement::getStringWidthInt (fonts::ui (type::body, fonts::Weight::semiBold), text) +
                 2 * space::s3 + space::s5;
}

juce::Font ThunderLookAndFeel::getLabelFont (juce::Label& label)
{
    return label.getFont();
}

juce::Font ThunderLookAndFeel::getAlertWindowMessageFont()
{
    return fonts::ui (type::body);
}

juce::Font ThunderLookAndFeel::getAlertWindowTitleFont()
{
    return fonts::ui (type::title, fonts::Weight::semiBold);
}

void ThunderLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor&)
{
    g.setColour (colour::bgRaised);
    g.fillRoundedRectangle (juce::Rectangle<int> (width, height).toFloat(), radius::sm);
}

void ThunderLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    g.setColour (editor.hasKeyboardFocus (true) ? colour::borderStrong : colour::borderSubtle);
    g.drawRoundedRectangle (juce::Rectangle<int> (width, height).toFloat().reduced (border::width * 0.5f), radius::sm,
                            border::width);
}

// ---- Tooltips (UI.md §9.14)
// ------------------------------------------------------------------------------------------

juce::AttributedString ThunderLookAndFeel::tooltipText (const juce::String& text)
{
    juce::AttributedString s;
    s.setWordWrap (juce::AttributedString::byWord);

    const auto firstLine = text.upToFirstOccurrenceOf ("\n", false, false);
    const auto rest = text.fromFirstOccurrenceOf ("\n", false, false);

    // "Name: what it does" → the name in bold.
    if (const auto colon = firstLine.indexOf (": "); colon > 0)
    {
        s.append (firstLine.substring (0, colon), fonts::ui (type::body, fonts::Weight::semiBold), colour::textPrimary);
        s.append (firstLine.substring (colon), fonts::ui (type::body), colour::textPrimary);
    }
    else
    {
        s.append (firstLine, fonts::ui (type::body), colour::textPrimary);
    }

    if (rest.isNotEmpty())
        s.append ("\n" + rest, fonts::mono (type::value), colour::textSecondary);

    return s;
}

juce::Rectangle<int> ThunderLookAndFeel::getTooltipBounds (const juce::String& text, juce::Point<int> screenPos,
                                                           juce::Rectangle<int> parentArea)
{
    const auto maxWidth = size::tooltipMaxWidth;
    juce::TextLayout layout;
    layout.createLayout (tooltipText (text), static_cast<float> (maxWidth - 2 * space::s2));

    const auto w = juce::jmin (maxWidth, static_cast<int> (std::ceil (layout.getWidth())) + 2 * space::s2);
    const auto h = static_cast<int> (std::ceil (layout.getHeight())) + 2 * space::s2;

    return juce::Rectangle<int> (
               screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + space::s3) : screenPos.x + space::s5,
               screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + space::s1) : screenPos.y + space::s1, w, h)
        .constrainedWithin (parentArea);
}

void ThunderLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto bounds = juce::Rectangle<int> (width, height).toFloat();
    g.setColour (colour::bgRaised);
    g.fillRoundedRectangle (bounds, radius::sm);
    g.setColour (colour::borderSubtle);
    g.drawRoundedRectangle (bounds.reduced (border::width * 0.5f), radius::sm, border::width);

    juce::TextLayout layout;
    layout.createLayout (tooltipText (text), bounds.getWidth() - 2.0f * space::s2);
    layout.draw (g, bounds.reduced (static_cast<float> (space::s2)));
}

void ThunderLookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h, bool isMouseOver, bool isMouseDragging)
{
    // Three short diagonal lines in the bottom-right corner (UI.md §7.1).
    const auto area = juce::Rectangle<int> (w, h).toFloat().reduced (static_cast<float> (space::s1) * 0.5f);
    g.setColour (isMouseOver || isMouseDragging ? colour::textSecondary : colour::textDisabled);
    for (int i = 1; i <= 3; ++i)
    {
        const auto t = static_cast<float> (i) / 3.0f;
        g.drawLine (area.getRight() - area.getWidth() * t, area.getBottom(), area.getRight(),
                    area.getBottom() - area.getHeight() * t, border::width);
    }
}

} // namespace thunder::ui
