#include "SettingsPanel.h"

#include "core/state/UserSettings.h"
#include "plugin/ThunderProcessor.h"
#include "plugin/ui/theme/ThunderFonts.h"
#include "plugin/ui/theme/ThunderTheme.h"

namespace thunder::ui
{

using namespace theme;

namespace
{
juce::String percent (float scale)
{
    return juce::String (juce::roundToInt (scale * 100.0f)) + "%";
}

constexpr int kRows = 5;

// The last parts of a long path ("…/Thunder/Captures"); the tooltip has all of it.
juce::String shortPath (const juce::File& folder)
{
    const auto parts = juce::StringArray::fromTokens (folder.getFullPathName(), juce::File::getSeparatorString(), "");
    if (parts.size() <= 3)
        return folder.getFullPathName();
    return juce::String::fromUTF8 ("…/") + parts[parts.size() - 2] + "/" + parts[parts.size() - 1];
}
} // namespace

// ---- PillToggle
// ------------------------------------------------------------------------------------------------------

SettingsPanel::PillToggle::PillToggle (const juce::String& title)
{
    setTitle (title);
    setClickingTogglesState (true);
    setMouseClickGrabsKeyboardFocus (false);
    setWantsKeyboardFocus (true);
}

void SettingsPanel::PillToggle::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto area = getLocalBounds().toFloat().removeFromRight (static_cast<float> (size::toggleWidth + space::s1));
    drawPillToggle (g, area, getToggleState(), highlighted, 0.0f, hasKeyboardFocus (false));
}

// ---- SettingsPanel
// ---------------------------------------------------------------------------------------------------

SettingsPanel::SettingsPanel (ThunderProcessor& p, float currentScale)
    : processor (p)
    , formatControl ("Capture format", {"24-bit", "32-bit float"}, [this] (int i) { setCaptureFormat (i == 1); })
{
    setTitle ("Settings");
    setWantsKeyboardFocus (true);

    addLabel ("UI scale");
    for (int i = 0; i < static_cast<int> (std::size (size::scalePresets)); ++i)
        scaleBox.addItem (percent (size::scalePresets[i]), i + 1);
    scaleBox.setTitle ("UI scale");
    scaleBox.setTooltip ("The size of Thunder's window. You can also drag its corner.");
    scaleBox.onChange = [this]
    {
        if (const auto index = scaleBox.getSelectedItemIndex(); index >= 0)
            chooseScale (size::scalePresets[index]);
    };
    // A size from dragging the corner shows as itself until a preset is chosen.
    scaleBox.setText (percent (currentScale), juce::dontSendNotification);
    for (int i = 0; i < static_cast<int> (std::size (size::scalePresets)); ++i)
        if (juce::approximatelyEqual (size::scalePresets[i], currentScale))
            scaleBox.setSelectedItemIndex (i, juce::dontSendNotification);
    addAndMakeVisible (scaleBox);

    addLabel ("Show tooltips");
    tooltipsToggle.setTooltip ("Show what a control does when the pointer rests on it");
    tooltipsToggle.onClick = [this] { setShowTooltips (tooltipsToggle.getToggleState()); };
    addAndMakeVisible (tooltipsToggle);

    addLabel ("Capture format");
    formatControl.setTooltip ("24-bit WAV, or 32-bit float WAV, for new captures");
    addAndMakeVisible (formatControl);

    addLabel ("Captures folder");
    folderText.setFont (fonts::ui (type::caption));
    folderText.setColour (juce::Label::textColourId, colour::textSecondary);
    folderText.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (folderText);
    changeFolderButton.setTooltip ("Choose where new captures are saved");
    changeFolderButton.onClick = [this] { chooseFolder(); };
    addAndMakeVisible (changeFolderButton);
    defaultFolderButton.setTooltip ("Use the default captures folder (Documents/Thunder/Captures)");
    defaultFolderButton.onClick = [this] { setCapturesFolder ({}); };
    addChildComponent (defaultFolderButton);

    addLabel ("Keep Key, Scale and Output Mode when loading presets");
    preserveToggle.setTooltip ("Loading a preset leaves your key, scale and output mode as they are");
    preserveToggle.onClick = [this] { setPreserveSession (preserveToggle.getToggleState()); };
    addAndMakeVisible (preserveToggle);

    refresh();
}

SettingsPanel::~SettingsPanel() = default;

juce::Label& SettingsPanel::addLabel (const juce::String& text)
{
    auto* label = labels.add (std::make_unique<juce::Label> (juce::String(), text));
    label->setFont (fonts::ui (type::body));
    label->setColour (juce::Label::textColourId, colour::textPrimary);
    label->setJustificationType (juce::Justification::centredLeft);
    label->setMinimumHorizontalScale (1.0f);
    label->setAccessible (false); // each control carries its own title
    addAndMakeVisible (label);
    return *label;
}

void SettingsPanel::refresh()
{
    const auto settings = state::UserSettings::load (processor.getSettingsFile());
    tooltipsToggle.setToggleState (settings.showTooltips, juce::dontSendNotification);
    formatControl.setSelectedOption (settings.captureFormat == capture::CaptureFormat::Float32 ? 1 : 0);
    preserveToggle.setToggleState (processor.getPresetManager().getPreserveSession(), juce::dontSendNotification);

    const auto folder = settings.getCapturesFolder();
    folderText.setText (shortPath (folder), juce::dontSendNotification);
    folderText.setTooltip (folder.getFullPathName());
    defaultFolderButton.setVisible (settings.capturesFolder != juce::File());
    resized();
}

juce::Rectangle<int> SettingsPanel::popoverBounds() const
{
    const auto height = space::s3 + size::labelHeight + space::s2 + kRows * size::settingsRowHeight + space::s3;
    return {getWidth() - space::s4 - size::settingsWidth, size::topBarHeight - space::s1, size::settingsWidth, height};
}

void SettingsPanel::paint (juce::Graphics& g)
{
    const auto box = popoverBounds();
    paint::drawShadow (g, box, elevation::popover);
    paint::fillRaised (g, box.toFloat(), radius::md, colour::bgRaised, colour::borderSubtle);

    auto area = box.reduced (space::s4, space::s3);
    g.setColour (colour::textPrimary);
    g.setFont (fonts::ui (type::title, fonts::Weight::semiBold).withExtraKerningFactor (type::titleTracking));
    g.drawText ("SETTINGS", area.removeFromTop (size::labelHeight), juce::Justification::centredLeft);
    area.removeFromTop (space::s2);

    g.setColour (colour::borderSubtle);
    for (int i = 0; i < kRows; ++i)
        g.drawHorizontalLine (area.getY() + i * size::settingsRowHeight, static_cast<float> (area.getX()),
                              static_cast<float> (area.getRight()));
}

void SettingsPanel::resized()
{
    auto area = popoverBounds().reduced (space::s4, space::s3);
    area.removeFromTop (size::labelHeight + space::s2);

    const auto nextRow = [&area] { return area.removeFromTop (size::settingsRowHeight); };
    const auto control = [] (juce::Rectangle<int>& r, int width, int height)
    {
        auto c = r.removeFromRight (width).withSizeKeepingCentre (width, height);
        r.removeFromRight (space::s2);
        return c;
    };

    // Each row: the control on the right, its label in the space left.
    for (int i = 0; i < labels.size(); ++i)
    {
        auto row = nextRow();
        if (i == 3)
            labels[i]->setBounds (row.removeFromLeft (size::settingsLabelWidth / 2));

        switch (i)
        {
            case 0:
                scaleBox.setBounds (control (row, size::barsDropdownWidth, size::controlHeight));
                break;
            case 1:
                tooltipsToggle.setBounds (control (row, size::toggleWidth + space::s3, size::controlHeight));
                break;
            case 2:
                formatControl.setBounds (control (row, 2 * size::segmentedSegment, size::controlHeight));
                break;
            case 3:
            {
                changeFolderButton.setBounds (control (row, size::dialogButtonWidth, size::controlHeight));
                if (defaultFolderButton.isVisible())
                    defaultFolderButton.setBounds (control (row, size::iconButton, size::iconButton));
                folderText.setBounds (row);
                break;
            }
            default:
                preserveToggle.setBounds (control (row, size::toggleWidth + space::s3, size::controlHeight));
                break;
        }

        if (i != 3)
            labels[i]->setBounds (row);
    }
}

void SettingsPanel::mouseUp (const juce::MouseEvent& e)
{
    // A click outside the popover closes it.
    if (!popoverBounds().contains (e.getPosition()))
        close();
}

bool SettingsPanel::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        close();
        return true;
    }
    return false;
}

void SettingsPanel::chooseScale (float scale)
{
    if (onScaleChosen)
        onScaleChosen (scale);
}

void SettingsPanel::setShowTooltips (bool show)
{
    auto settings = state::UserSettings::load (processor.getSettingsFile());
    settings.showTooltips = show;
    settings.save (processor.getSettingsFile());
    tooltipsToggle.setToggleState (show, juce::dontSendNotification);
    if (onShowTooltips)
        onShowTooltips (show);
}

void SettingsPanel::setCaptureFormat (bool float32)
{
    auto settings = state::UserSettings::load (processor.getSettingsFile());
    settings.captureFormat = float32 ? capture::CaptureFormat::Float32 : capture::CaptureFormat::Int24;
    settings.save (processor.getSettingsFile());
    refresh();
}

void SettingsPanel::setCapturesFolder (const juce::File& folder)
{
    auto settings = state::UserSettings::load (processor.getSettingsFile());
    settings.capturesFolder = folder;
    settings.save (processor.getSettingsFile());
    refresh();
}

void SettingsPanel::setPreserveSession (bool preserve)
{
    processor.getPresetManager().setPreserveSession (preserve);
    refresh();
}

void SettingsPanel::chooseFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose the captures folder", processor.getCapturesFolder());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [safe = juce::Component::SafePointer<SettingsPanel> (this)] (const juce::FileChooser& fc)
                          {
                              const auto folder = fc.getResult();
                              if (safe == nullptr || folder == juce::File() || !folder.isDirectory())
                                  return;
                              safe->setCapturesFolder (folder);
                          });
}

void SettingsPanel::close()
{
    if (onClose)
        onClose();
}

} // namespace thunder::ui
