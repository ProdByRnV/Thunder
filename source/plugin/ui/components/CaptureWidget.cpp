#include "CaptureWidget.h"

#include "core/state/UserSettings.h"
#include "plugin/ThunderProcessor.h"
#include "plugin/ui/theme/ThunderTheme.h"

namespace thunder::ui
{

namespace
{
constexpr int kBarChoices[] = {1, 2, 4, 8, 16};

using Phase = capture::CaptureStatus::Phase;
} // namespace

// The capture result: status text, a progress bar while recording, and the
// drag source for drag-out (UI.md §16.1).
class CaptureWidget::Chip final
    : public juce::Component
    , public juce::SettableTooltipClient
{
public:
    explicit Chip (CaptureWidget& w)
        : owner (w)
    {
        setRepaintsOnMouseActivity (true);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& s = owner.status;
        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);

        g.setColour (theme::colour::bgPanel);
        g.fillRoundedRectangle (bounds, theme::radius::sm);

        if (s.phase == Phase::Recording)
        {
            g.setColour (theme::colour::accentPrimaryDim);
            g.fillRoundedRectangle (bounds.withWidth (bounds.getWidth() * s.progress), theme::radius::sm);

            // One segment per bar.
            g.setColour (theme::colour::borderSubtle);
            for (int bar = 1; bar < s.bars; ++bar)
            {
                const auto x =
                    bounds.getX() + bounds.getWidth() * static_cast<float> (bar) / static_cast<float> (s.bars);
                g.drawVerticalLine (juce::roundToInt (x), bounds.getY(), bounds.getBottom());
            }
        }

        const auto highlight = s.phase == Phase::Ready && juce::Time::getMillisecondCounter() < owner.successUntil;
        g.setColour (highlight ? theme::colour::stateSuccess : theme::colour::borderSubtle);
        g.drawRoundedRectangle (bounds, theme::radius::sm, 1.0f);

        auto text = getLocalBounds().reduced (theme::space::s2, 0);
        if (owner.getDraggableFile() != juce::File())
        {
            // ⠿ drag handle
            g.setColour (theme::colour::textSecondary);
            g.setFont (juce::FontOptions (theme::type::body));
            g.drawText (juce::String::fromUTF8 ("\xe2\xa0\xbf"), text.removeFromLeft (theme::space::s3),
                        juce::Justification::centredLeft);
            text.removeFromLeft (theme::space::s1);
        }

        g.setColour (s.phase == Phase::Failed ? theme::colour::stateError : theme::colour::textPrimary);
        g.setFont (juce::FontOptions (theme::type::body));
        g.drawFittedText (owner.getChipText(), text, juce::Justification::centredLeft, 1);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragStarted = false;
        if (e.mods.isPopupMenu())
            owner.showChipMenu();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto file = owner.getDraggableFile();
        if (dragStarted || file == juce::File() || e.mods.isPopupMenu() ||
            e.getDistanceFromDragStart() <= theme::size::dragThreshold)
            return;

        dragStarted = true;
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({file.getFullPathName()}, false, this);
    }

    void mouseUp (const juce::MouseEvent&) override { dragStarted = false; }

    juce::MouseCursor getMouseCursor() override
    {
        return owner.getDraggableFile() != juce::File() ? juce::MouseCursor::DraggingHandCursor
                                                        : juce::MouseCursor::NormalCursor;
    }

private:
    CaptureWidget& owner;
    bool dragStarted = false;
};

CaptureWidget::CaptureWidget (ThunderProcessor& p)
    : processor (p)
    , chip (std::make_unique<Chip> (*this))
{
    title.setText ("CAPTURE", juce::dontSendNotification);
    title.setFont (juce::FontOptions (theme::type::label));
    title.setColour (juce::Label::textColourId, theme::colour::textSecondary);
    addAndMakeVisible (title);

    for (const auto bars : kBarChoices)
        barsBox.addItem (juce::String (bars) + (bars == 1 ? " bar" : " bars"), bars);
    barsBox.setTooltip ("Capture length");
    barsBox.onChange = [this] { writeSessionChoices(); };
    addAndMakeVisible (barsBox);

    sourceBox.addItem ("Layer", 1);
    sourceBox.addItem ("Full Mix", 2);
    sourceBox.setTooltip ("Layer: the layer alone. Full Mix: everything Thunder outputs.");
    sourceBox.onChange = [this] { writeSessionChoices(); };
    addAndMakeVisible (sourceBox);

    armButton.setTooltip ("Recording starts at the next bar while your DAW plays");
    armButton.onClick = [this] { toggleArm(); };
    addAndMakeVisible (armButton);

    chip->setTooltip ("Drag into your DAW. Right-click for Save as and Show in folder.");
    addAndMakeVisible (*chip);

    folderButton.setButtonText ("Folder");
    folderButton.setTooltip ("Open the captures folder");
    folderButton.onClick = [this] { openCapturesFolder(); };
    addAndMakeVisible (folderButton);

    menuButton.setButtonText (juce::String::fromUTF8 ("\xe2\x8b\xaf"));
    menuButton.setTooltip ("Capture format and folder");
    menuButton.onClick = [this] { showSettingsMenu(); };
    addAndMakeVisible (menuButton);

    // Show the session's choices, and the last capture if there is one.
    const auto session = processor.getSessionState();
    barsBox.setSelectedId (session.captureBars, juce::dontSendNotification);
    sourceBox.setSelectedId (session.captureSource == state::CaptureSource::FullMix ? 2 : 1,
                             juce::dontSendNotification);
    seenCompleted = processor.getCaptureWriter().getCompletedCount();

    refresh();
    startTimerHz (theme::timing::uiRefreshHz);
}

CaptureWidget::~CaptureWidget() = default;

void CaptureWidget::paint (juce::Graphics& g)
{
    g.fillAll (theme::colour::bgPanel);
    g.setColour (theme::colour::borderSubtle);
    g.drawHorizontalLine (0, 0.0f, static_cast<float> (getWidth()));

    if (status.phase == Phase::Recording || status.phase == Phase::Armed || status.phase == Phase::WaitingForPlay)
    {
        // Recording dot next to the Arm button.
        const auto dot =
            armButton.getBounds()
                .toFloat()
                .withWidth (static_cast<float> (theme::space::s2))
                .withSizeKeepingCentre (static_cast<float> (theme::space::s2), static_cast<float> (theme::space::s2))
                .translated (-static_cast<float> (theme::space::s3), 0.0f);
        g.setColour (theme::colour::stateRecording);
        g.fillEllipse (dot);
    }
}

void CaptureWidget::resized()
{
    auto row = getLocalBounds().reduced (theme::space::s3, 0);
    row = row.withSizeKeepingCentre (row.getWidth(), theme::size::controlHeight);

    title.setBounds (row.removeFromLeft (theme::size::captureTitleWidth));
    barsBox.setBounds (row.removeFromLeft (theme::size::barsDropdownWidth));
    row.removeFromLeft (theme::space::s2);
    sourceBox.setBounds (row.removeFromLeft (theme::size::sourceDropdownWidth));
    row.removeFromLeft (theme::space::s3 + theme::space::s3);
    armButton.setBounds (row.removeFromLeft (theme::size::armButtonWidth));

    menuButton.setBounds (row.removeFromRight (theme::size::menuButtonWidth));
    row.removeFromRight (theme::space::s1);
    folderButton.setBounds (row.removeFromRight (theme::size::iconButtonWidth));
    row.removeFromRight (theme::space::s2);
    row.removeFromLeft (theme::space::s3);
    chip->setBounds (row);
}

void CaptureWidget::timerCallback()
{
    refresh();
}

void CaptureWidget::refresh()
{
    status = processor.getCaptureStatus();

    if (const auto completed = processor.getCaptureWriter().getCompletedCount(); completed != seenCompleted)
    {
        seenCompleted = completed;
        successUntil =
            juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (theme::timing::successHighlightMs);
    }

    switch (status.phase)
    {
        case Phase::WaitingForPlay:
        case Phase::Armed:
            armButton.setButtonText ("Armed");
            break;
        case Phase::Recording:
            armButton.setButtonText ("Stop");
            break;
        case Phase::Idle:
        case Phase::Saving:
        case Phase::Ready:
        case Phase::Failed:
            armButton.setButtonText ("Arm");
            break;
    }

    armButton.setEnabled (status.phase != Phase::Saving);
    barsBox.setEnabled (status.phase != Phase::Recording && status.phase != Phase::Saving);
    sourceBox.setEnabled (barsBox.isEnabled());

    repaint();
}

juce::String CaptureWidget::getChipText() const
{
    return status.describe();
}

juce::File CaptureWidget::getDraggableFile() const
{
    if (status.phase != Phase::Ready && status.phase != Phase::Idle)
        return {};

    return status.file.existsAsFile() ? status.file : juce::File();
}

void CaptureWidget::toggleArm()
{
    if (status.isActive())
        processor.cancelCapture();
    else if (status.phase != Phase::Saving)
        processor.armCapture();

    refresh();
}

void CaptureWidget::writeSessionChoices()
{
    auto session = processor.getSessionState();
    session.captureBars = barsBox.getSelectedId() > 0 ? barsBox.getSelectedId() : session.captureBars;
    session.captureSource =
        sourceBox.getSelectedId() == 2 ? state::CaptureSource::FullMix : state::CaptureSource::Layer;
    processor.setSessionState (session);

    // An armed capture takes the new length and source.
    if (status.phase == Phase::Armed || status.phase == Phase::WaitingForPlay)
        processor.armCapture();
}

void CaptureWidget::showChipMenu()
{
    const auto file = getDraggableFile();
    if (file == juce::File())
        return;

    juce::PopupMenu menu;
    menu.addItem (juce::String::fromUTF8 ("Save as\xe2\x80\xa6"), [this] { saveAs(); });
    menu.addItem ("Show in folder", [this] { showInFolder(); });
    menu.addSeparator();
    menu.addItem ("Delete capture", [this] { deleteCapture(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (chip.get()));
}

void CaptureWidget::showInFolder()
{
    if (const auto file = getDraggableFile(); file != juce::File())
        file.revealToUser();
}

void CaptureWidget::saveAs()
{
    const auto file = getDraggableFile();
    if (file == juce::File())
        return;

    const auto start =
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (file.getFileName());
    chooser = std::make_unique<juce::FileChooser> ("Save the capture as", start, "*.wav");

    const auto chooserFlags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                              juce::FileBrowserComponent::warnAboutOverwriting;

    chooser->launchAsync (chooserFlags,
                          [file] (const juce::FileChooser& fc)
                          {
                              auto target = fc.getResult();
                              if (target == juce::File() || target == file)
                                  return;

                              if (!target.hasFileExtension ("wav"))
                                  target = target.withFileExtension ("wav");

                              if (!file.copyFileTo (target))
                                  juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                                          "Thunder",
                                                                          "The capture couldn't be saved there.");
                          });
}

void CaptureWidget::deleteCapture()
{
    processor.getCaptureWriter().deleteLastCapture();
    refresh();
}

void CaptureWidget::openCapturesFolder()
{
    const auto folder = processor.getCapturesFolder();
    if (folder.createDirectory())
        folder.startAsProcess();
}

void CaptureWidget::showSettingsMenu()
{
    const auto settings = state::UserSettings::load (processor.getSettingsFile());
    const auto is32 = settings.captureFormat == capture::CaptureFormat::Float32;

    auto setFormat = [this] (capture::CaptureFormat format)
    {
        auto s = state::UserSettings::load (processor.getSettingsFile());
        s.captureFormat = format;
        s.save (processor.getSettingsFile());
    };

    juce::PopupMenu format;
    format.addItem ("24-bit", true, !is32, [setFormat] { setFormat (capture::CaptureFormat::Int24); });
    format.addItem ("32-bit float", true, is32, [setFormat] { setFormat (capture::CaptureFormat::Float32); });

    juce::PopupMenu menu;
    menu.addSubMenu ("Capture format", format);
    menu.addItem (juce::String::fromUTF8 ("Choose the captures folder\xe2\x80\xa6"),
                  [this] { chooseCapturesFolder(); });

    if (settings.capturesFolder != juce::File())
        menu.addItem ("Use the default captures folder",
                      [this]
                      {
                          auto s = state::UserSettings::load (processor.getSettingsFile());
                          s.capturesFolder = juce::File();
                          s.save (processor.getSettingsFile());
                      });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menuButton));
}

void CaptureWidget::chooseCapturesFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose the captures folder", processor.getCapturesFolder());

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto folder = fc.getResult();
                              if (folder == juce::File() || !folder.isDirectory())
                                  return;

                              auto s = state::UserSettings::load (processor.getSettingsFile());
                              s.capturesFolder = folder;
                              s.save (processor.getSettingsFile());
                          });
}

} // namespace thunder::ui
