#include "ThunderLookAndFeel.h"

#include "ThunderFonts.h"
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

void ThunderLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                           float startAngle, float endAngle, juce::Slider& slider)
{
    // C-KNOB, flat (UI.md §9.1, §15): track, value arc, face, pointer.
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    const auto diameter = std::min (bounds.getWidth(), bounds.getHeight());
    const auto radius = diameter * 0.5f - size::knobTrack;
    const auto centre = bounds.getCentre();
    const auto angle = startAngle + sliderPos * (endAngle - startAngle);
    const auto enabled = slider.isEnabled();

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour (colour::borderSubtle);
    g.strokePath (track,
                  juce::PathStrokeType (size::knobTrack, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const auto bipolar = static_cast<bool> (slider.getProperties()[bipolarProperty]);
    const auto from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
    if (std::abs (angle - from) > 0.001f)
    {
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, std::min (from, angle), std::max (from, angle),
                           true);
        g.setColour (enabled ? colour::accentPrimary : colour::textDisabled);
        g.strokePath (
            arc, juce::PathStrokeType (size::knobTrack, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const auto face = juce::Rectangle<float> (diameter * size::knobFace, diameter * size::knobFace).withCentre (centre);
    g.setColour (colour::bgRaised);
    g.fillEllipse (face);
    g.setColour (colour::borderSubtle);
    g.drawEllipse (face, border::width);

    const auto pointerFrom = centre.getPointOnCircumference (diameter * 0.5f * size::knobPointerFrom, angle);
    const auto pointerTo = centre.getPointOnCircumference (diameter * 0.5f * size::knobPointerTo, angle);
    g.setColour (enabled ? colour::textPrimary : colour::textDisabled);
    g.drawLine ({pointerFrom, pointerTo}, size::knobPointer);
}

void ThunderLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool)
{
    // C-CHECKBOX (UI.md §9.8): a 14 px box, then the label; the whole row clicks.
    const auto boxSize = static_cast<float> (size::checkboxBox);
    const auto box = juce::Rectangle<float> (boxSize, boxSize)
                         .withPosition (0.0f, (static_cast<float> (button.getHeight()) - boxSize) * 0.5f)
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
    }

    g.setColour (colour::borderStrong);
    g.drawRoundedRectangle (box, radius::sm, border::width);

    g.setColour (button.isEnabled() ? (highlighted ? colour::textPrimary : colour::textSecondary)
                                    : colour::textDisabled);
    g.setFont (fonts::ui (type::label, fonts::Weight::medium));
    g.drawFittedText (button.getButtonText(), button.getLocalBounds().withTrimmedLeft (size::checkboxBox + space::s2),
                      juce::Justification::centredLeft, 1);
}

void ThunderLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& background,
                                               bool highlighted, bool down)
{
    // C-BUTTON (UI.md §9.6): secondary style unless the button is "on".
    const auto bounds = button.getLocalBounds().toFloat().reduced (border::width * 0.5f);
    auto fill = button.getToggleState() ? colour::accentPrimary : background;
    if (!button.getToggleState() && (highlighted || down))
        fill = colour::bgHover;

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, radius::sm);
    g.setColour (button.hasKeyboardFocus (false) ? colour::borderStrong : colour::borderSubtle);
    g.drawRoundedRectangle (bounds, radius::sm, border::width);
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
    g.setColour (box.isMouseOver (true) && box.isEnabled() ? colour::bgHover : colour::bgRaised);
    g.fillRoundedRectangle (bounds, radius::sm);
    g.setColour (box.hasKeyboardFocus (true) ? colour::borderStrong : colour::borderSubtle);
    g.drawRoundedRectangle (bounds, radius::sm, border::width);

    const auto chevronArea = juce::Rectangle<float> (static_cast<float> (height), static_cast<float> (height))
                                 .withRightX (bounds.getRight())
                                 .reduced (static_cast<float> (height) * 0.36f);
    juce::Path chevron;
    chevron.startNewSubPath (chevronArea.getX(), chevronArea.getY() + chevronArea.getHeight() * 0.3f);
    chevron.lineTo (chevronArea.getCentreX(), chevronArea.getBottom() - chevronArea.getHeight() * 0.2f);
    chevron.lineTo (chevronArea.getRight(), chevronArea.getY() + chevronArea.getHeight() * 0.3f);
    g.setColour (box.isEnabled() ? colour::textSecondary : colour::textDisabled);
    g.strokePath (chevron, juce::PathStrokeType (border::width * 1.5f));
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

juce::Font ThunderLookAndFeel::getPopupMenuFont()
{
    return fonts::ui (type::body);
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

juce::Rectangle<int> ThunderLookAndFeel::getTooltipBounds (const juce::String& text, juce::Point<int> screenPos,
                                                           juce::Rectangle<int> parentArea)
{
    // C-TOOLTIP (UI.md §9.14): up to 260 wide, body type.
    const auto maxWidth = size::tooltipMaxWidth;
    juce::TextLayout layout;
    juce::AttributedString s;
    s.append (text, fonts::ui (type::body), colour::textPrimary);
    s.setWordWrap (juce::AttributedString::byWord);
    layout.createLayout (s, static_cast<float> (maxWidth - 2 * space::s2));

    const auto w = juce::jmin (maxWidth, static_cast<int> (std::ceil (layout.getWidth())) + 2 * space::s2);
    const auto h = static_cast<int> (std::ceil (layout.getHeight())) + 2 * space::s1 + space::s1;

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

    juce::AttributedString s;
    s.append (text, fonts::ui (type::body), colour::textPrimary);
    s.setWordWrap (juce::AttributedString::byWord);
    juce::TextLayout layout;
    layout.createLayout (s, bounds.getWidth() - 2.0f * space::s2);
    layout.draw (g, bounds.reduced (static_cast<float> (space::s2), static_cast<float> (space::s1)));
}

} // namespace thunder::ui
