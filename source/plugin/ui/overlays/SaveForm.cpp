#include "SaveForm.h"

#include "plugin/ui/theme/ThunderFonts.h"
#include "plugin/ui/theme/ThunderTheme.h"

#include <set>

namespace thunder::ui
{

using namespace theme;

namespace
{
void styleField (juce::TextEditor& field, int maxLength)
{
    field.setFont (fonts::ui (type::body));
    field.setInputRestrictions (maxLength);
    field.setIndents (space::s2, space::s1);
}

void styleLabel (juce::Label& label, const juce::String& text)
{
    label.setText (text.toUpperCase(), juce::dontSendNotification);
    label.setFont (fonts::ui (type::label, fonts::Weight::medium));
    label.setColour (juce::Label::textColourId, colour::textSecondary);
}
} // namespace

SaveForm::SaveForm (PresetManager& manager, bool saveAs)
    : presets (manager)
{
    setWantsKeyboardFocus (true);
    setTitle (saveAs ? "Save preset as" : "Save preset");

    title.setText (saveAs ? juce::String::fromUTF8 ("Save as\xe2\x80\xa6") : juce::String ("Save preset"),
                   juce::dontSendNotification);
    title.setFont (fonts::ui (type::title, fonts::Weight::semiBold));
    addAndMakeVisible (title);

    styleLabel (nameLabel, "Name");
    styleLabel (authorLabel, "Author");
    styleLabel (categoryLabel, "Category");
    styleLabel (descriptionLabel, "Description");

    styleField (name, size::maxTextLength);
    styleField (author, size::maxTextLength);
    styleField (description, size::maxDescriptionLength);
    description.setMultiLine (true, true);
    description.setReturnKeyStartsNewLine (true);
    description.setTextToShowWhenEmpty ("What the sound does, and what to try with it", colour::textDisabled);
    name.setTitle ("Name");
    author.setTitle ("Author");
    description.setTitle ("Description");

    // Category: free text, suggesting the categories already in use (one spelling each).
    category.setEditableText (true);
    category.setTitle ("Category");
    std::set<juce::String> categories;
    for (const auto& entry : presets.getListing().entries)
        if (entry.category.isNotEmpty())
            categories.insert (entry.category);
    int id = 1;
    for (const auto& c : categories)
        category.addItem (c, id++);

    // Prefill: Save keeps the loaded preset's details; Save as starts afresh.
    const auto& loaded = presets.getLoadedMetadata();
    const auto current = presets.getCurrent();
    author.setText (current.isUser() && loaded.author.isNotEmpty() ? loaded.author : presets.getRememberedAuthor(),
                    false);
    if (!saveAs)
    {
        name.setText (current.name, false);
        category.setText (loaded.category, juce::dontSendNotification);
        description.setText (loaded.description, false);
    }

    name.onTextChange = [this]
    {
        setAskingToReplace (false);
        setMessage ({}, false);
    };
    name.onReturnKey = [this] { submit(); };
    for (auto* field : {&name, &author, &description})
        field->onEscapeKey = [this] { cancel(); };
    author.onReturnKey = [this] { submit(); };

    cancelButton.setButtonText ("Cancel");
    cancelButton.onClick = [this] { cancel(); };
    saveButton.setButtonText ("Save");
    saveButton.setToggleState (true, juce::dontSendNotification); // primary
    saveButton.onClick = [this] { submit(); };
    replaceButton.setButtonText ("Replace it");
    replaceButton.setColour (juce::TextButton::textColourOffId, colour::stateError);
    replaceButton.onClick = [this] { confirmReplace(); };

    for (auto* c :
         std::initializer_list<juce::Component*>{&nameLabel, &name, &authorLabel, &author, &categoryLabel, &category,
                                                 &descriptionLabel, &description, &cancelButton, &saveButton})
        addAndMakeVisible (c);
    addChildComponent (replaceButton);
}

SaveForm::~SaveForm() = default;

juce::Rectangle<int> SaveForm::panelBounds() const
{
    const auto height = space::s4 + size::controlHeight + space::s2 + 3 * (size::controlHeight + space::s2) +
                        size::saveFormDescriptionHeight + space::s3 + size::labelHeight + space::s2 +
                        size::controlHeight + space::s4;
    return getLocalBounds().withSizeKeepingCentre (size::saveFormWidth, height);
}

void SaveForm::paint (juce::Graphics& g)
{
    g.fillAll (colour::bgOverlay);

    const auto panel = panelBounds().toFloat();
    g.setColour (colour::bgPanel);
    g.fillRoundedRectangle (panel, radius::lg);
    g.setColour (colour::borderSubtle);
    g.drawRoundedRectangle (panel.reduced (border::width * 0.5f), radius::lg, border::width);

    if (message.isNotEmpty())
    {
        g.setColour (messageIsError ? colour::stateError : colour::textSecondary);
        g.setFont (fonts::ui (type::caption));
        g.drawFittedText (message,
                          cancelButton.getBounds()
                              .withY (cancelButton.getY() - space::s2 - size::labelHeight)
                              .withHeight (size::labelHeight)
                              .withLeft (static_cast<int> (panel.getX()) + space::s4)
                              .withRight (static_cast<int> (panel.getRight()) - space::s4),
                          juce::Justification::centredLeft, 1);
    }
}

void SaveForm::resized()
{
    auto area = panelBounds().reduced (space::s4);
    title.setBounds (area.removeFromTop (size::controlHeight));
    area.removeFromTop (space::s2);

    auto row = [&area] (juce::Label& label, juce::Component& field, int height)
    {
        auto r = area.removeFromTop (height);
        label.setBounds (r.removeFromLeft (size::saveFormLabelWidth).withHeight (size::controlHeight));
        field.setBounds (r);
        area.removeFromTop (space::s2);
    };
    row (nameLabel, name, size::controlHeight);
    row (authorLabel, author, size::controlHeight);
    row (categoryLabel, category, size::controlHeight);
    row (descriptionLabel, description, size::saveFormDescriptionHeight);

    area.removeFromTop (space::s1 + size::labelHeight); // the message line
    auto buttons = area.removeFromBottom (size::controlHeight);
    saveButton.setBounds (buttons.removeFromRight (size::iconButtonWidth + space::s4));
    buttons.removeFromRight (space::s2);
    replaceButton.setBounds (saveButton.getBounds());
    cancelButton.setBounds (buttons.removeFromRight (size::iconButtonWidth + space::s4));
}

bool SaveForm::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        cancel();
        return true;
    }
    if (key == juce::KeyPress::returnKey && !description.hasKeyboardFocus (false))
    {
        isAskingToReplace() ? confirmReplace() : submit();
        return true;
    }
    return false;
}

presets::Metadata SaveForm::collect() const
{
    presets::Metadata m;
    m.name = name.getText();
    m.author = author.getText();
    m.category = category.getText();
    m.description = description.getText();
    return presets::sanitise (m);
}

void SaveForm::submit()
{
    const auto meta = collect();
    if (meta.name.isEmpty() || !presets.fileForName (meta.name))
    {
        setMessage (presets::describe (presets::SaveResult::invalidName), true);
        return;
    }

    if (presets.wouldReplace (meta.name))
    {
        setMessage (juce::String::fromUTF8 ("\xe2\x80\x9c") + meta.name + juce::String::fromUTF8 ("\xe2\x80\x9d") +
                        " already exists.",
                    true);
        replaceName = meta.name;
        setAskingToReplace (true);
        return;
    }

    save();
}

void SaveForm::confirmReplace()
{
    // Only ever the file the prompt named: a changed name asks again.
    if (isAskingToReplace())
        save();
    else
        submit();
}

bool SaveForm::isAskingToReplace() const
{
    return askingToReplace && collect().name == replaceName;
}

void SaveForm::save()
{
    const auto result = presets.saveAs (collect());
    if (result != presets::SaveResult::ok)
    {
        setAskingToReplace (false);
        setMessage (presets::describe (result), true);
        return;
    }

    if (onClose)
        onClose (true);
}

void SaveForm::cancel()
{
    if (onClose)
        onClose (false);
}

void SaveForm::setMessage (const juce::String& text, bool isError)
{
    message = text;
    messageIsError = isError;
    repaint();
}

void SaveForm::setAskingToReplace (bool asking)
{
    askingToReplace = asking;
    replaceButton.setVisible (asking);
    saveButton.setVisible (!asking);
}

// ---- Toast
// -------------------------------------------------------------------------------------------------------------

Toast::Toast()
{
    setVisible (false);
    setTitle ("Message");
}

void Toast::show (const juce::String& newText, bool isError)
{
    text = newText;
    error = isError;
    setVisible (true);
    toFront (false);
    repaint();

    stopTimer();
    if (!isError)
        startTimer (timing::toastMs);
}

void Toast::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (border::width * 0.5f);
    g.setColour (colour::bgRaised);
    g.fillRoundedRectangle (bounds, radius::md);
    g.setColour (error ? colour::stateError : colour::borderSubtle);
    g.drawRoundedRectangle (bounds, radius::md, border::width);

    g.setColour (colour::textPrimary);
    g.setFont (fonts::ui (type::body));
    g.drawFittedText (text, getLocalBounds().reduced (space::s3, space::s1), juce::Justification::centredLeft, 2);
}

void Toast::mouseUp (const juce::MouseEvent&)
{
    setVisible (false);
}

void Toast::timerCallback()
{
    stopTimer();
    setVisible (false);
}

} // namespace thunder::ui
