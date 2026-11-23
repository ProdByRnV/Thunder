// The interface for key detection, sources and capture options (UI.md §9.10,
// §9.13, §16.2, §16.4; PRD FR-KEY-003, FR-INPUT-001/005, FR-CAPT-004): the
// badge shows how sure detection is, only "Use" changes the key, files dragged
// over the window are accepted or refused, the source strip shows the source,
// a missing source offers Locate and Dismiss, and the capture bar offers Input,
// Include tail and Loop. Headless: no window is shown.

#include "support/TestSettings.h"

#include "core/source/SourceLoader.h"
#include "core/state/StateIds.h"
#include "plugin/ThunderEditor.h"
#include "plugin/ThunderProcessor.h"
#include "plugin/ui/ParamUi.h"
#include "plugin/ui/theme/ThunderTheme.h"

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <optional>

using namespace thunder;
using analysis::AnalysisWorker;
using params::ParamId;
using DropHint = ui::VisualiserRegion::DropHint;

namespace
{
struct TempFolder
{
    juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("ThunderSourceUiTests-" + juce::Uuid().toString());
    TempFolder() { folder.createDirectory(); }
    ~TempFolder() { folder.deleteRecursively(); }
};

struct Fixture
{
    TempFolder temp;
    ThunderProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editorOwner;
    ThunderEditor* editor = nullptr;

    Fixture()
    {
        processor.setSettingsFile (temp.folder.getChildFile ("settings.xml"));
        test::chooseMotion (temp.folder.getChildFile ("settings.xml"));
        processor.getPresetManager().setUserFolder (temp.folder.getChildFile ("Presets"));
        processor.prepareToPlay (48000.0, 512);
        editorOwner.reset (processor.createEditorAndMakeActive());
        editor = dynamic_cast<ThunderEditor*> (editorOwner.get());
        REQUIRE (editor != nullptr);
    }

    float value (ParamId id)
    {
        return processor.getValueTreeState().getRawParameterValue (ui::paramIdString (id))->load();
    }
};

bool waitFor (const std::function<bool()>& condition)
{
    for (int i = 0; i < 1000 && !condition(); ++i)
        juce::Thread::sleep (10);
    return condition();
}

// A few seconds of a D minor tune.
juce::File writeTune (const juce::File& folder, const juce::String& name)
{
    constexpr double notes[] = {62, 65, 69, 67, 65, 64, 62, 57};
    juce::AudioBuffer<float> audio (1, 48000 * 6);
    for (int i = 0; i < audio.getNumSamples(); ++i)
    {
        const auto note = notes[(i / 24000) % 8];
        const auto hz = 440.0 * std::pow (2.0, (note - 69.0) / 12.0);
        double v = 0.0;
        for (int h = 1; h <= 4; ++h)
            v += std::sin (2.0 * juce::MathConstants<double>::pi * hz * h * i / 48000.0) / h;
        audio.setSample (0, i, static_cast<float> (0.25 * v));
    }

    const auto file = folder.getChildFile (name);
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    auto writer = juce::WavAudioFormat().createWriterFor (
        stream, juce::AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (24));
    writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    return file;
}

juce::StringArray itemTexts (const juce::PopupMenu& menu)
{
    juce::StringArray texts;
    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        texts.add (it.getItem().text);
    return texts;
}

// A copy: the iterator's items live only as long as the iterator.
std::optional<juce::PopupMenu::Item> findItem (const juce::PopupMenu& menu, int id)
{
    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        if (it.getItem().itemID == id)
            return it.getItem();
    return std::nullopt;
}

analysis::KeyEstimate estimate (params::KeyRoot root, params::Scale scale, float confidence)
{
    analysis::KeyEstimate e;
    e.valid = true;
    e.root = root;
    e.scale = scale;
    e.confidence = confidence;
    e.seconds = 10.0;
    return e;
}
} // namespace

// ---------------------------------------------------------------------------
// The key badge and menu (UI.md §9.13, FR-KEY-003)
// ---------------------------------------------------------------------------

TEST_CASE ("The key badge shows how sure detection is", "[unit][ui][key]")
{
    using namespace ui::theme;
    ui::KeyBadge badge;
    CHECK (badge.getDotColour() == colour::textDisabled);
    CHECK (badge.getBadgeText() == juce::String::fromUTF8 ("\xe2\x80\x93"));
    CHECK (badge.getTooltip().contains ("off"));

    badge.setShown ({estimate (params::KeyRoot::A, params::Scale::NaturalMinor, 0.87f), "the input", true});
    CHECK (badge.getBadgeText() == "87%");
    CHECK (badge.getDotColour() == colour::accentPrimary);
    CHECK (badge.getTooltip().contains ("A minor"));
    CHECK (badge.getDescription().contains ("the input"));

    badge.setShown ({estimate (params::KeyRoot::A, params::Scale::NaturalMinor, 0.55f), "the input", true});
    CHECK (badge.getDotColour() == colour::stateWarning);

    badge.setShown ({estimate (params::KeyRoot::A, params::Scale::NaturalMinor, 0.2f), "the input", true});
    CHECK (badge.getDotColour() == colour::textDisabled);

    badge.setShown ({{}, "the input", true});
    CHECK (badge.getTooltip().contains ("Listening"));
}

TEST_CASE ("Only Use changes the key; Keep and detection leave it", "[unit][ui][key]")
{
    Fixture f;
    auto& top = f.editor->getTopBar();
    top.getKeyBadge().setShown ({estimate (params::KeyRoot::FSharp, params::Scale::Major, 0.9f), "loop.wav", true});

    const auto texts = itemTexts (top.buildKeyMenu());
    CHECK (texts.contains ("Detected from loop.wav: F# major (90%)"));
    CHECK (texts.contains ("Use F# major"));
    CHECK (texts.contains ("Keep C Natural Minor"));

    top.chooseKeyMenuItem (ui::TopBar::kMenuKeepManual);
    CHECK (juce::exactlyEqual (f.value (ParamId::keyRoot), 0.0f));
    CHECK (juce::exactlyEqual (f.value (ParamId::keyScale), 1.0f));

    top.chooseKeyMenuItem (ui::TopBar::kMenuUseDetected);
    CHECK (juce::exactlyEqual (f.value (ParamId::keyRoot), 6.0f));
    CHECK (juce::exactlyEqual (f.value (ParamId::keyScale), 0.0f));

    // Nothing detected: nothing to use.
    top.getKeyBadge().setShown ({{}, "the input", true});
    CHECK_FALSE (findItem (top.buildKeyMenu(), ui::TopBar::kMenuUseDetected).has_value());
    top.chooseKeyMenuItem (ui::TopBar::kMenuUseDetected);
    CHECK (juce::exactlyEqual (f.value (ParamId::keyRoot), 6.0f));
}

TEST_CASE ("Auto Detect is switched in the key menu, for this project", "[unit][ui][key]")
{
    Fixture f;
    auto& top = f.editor->getTopBar();
    const auto item = findItem (top.buildKeyMenu(), ui::TopBar::kMenuAutoDetect);
    REQUIRE (item.has_value());
    CHECK (item->isTicked);

    top.chooseKeyMenuItem (ui::TopBar::kMenuAutoDetect);
    CHECK_FALSE (f.processor.getSessionState().keyAutoDetect);
    CHECK_FALSE (f.processor.getAnalysisWorker().isLiveDetectionOn());
    CHECK_FALSE (top.getKeyBadge().getShown().listening);
    CHECK_FALSE (findItem (top.buildKeyMenu(), ui::TopBar::kMenuAutoDetect)->isTicked);

    top.chooseKeyMenuItem (ui::TopBar::kMenuAutoDetect);
    CHECK (f.processor.getSessionState().keyAutoDetect);
    CHECK (f.processor.getAnalysisWorker().isLiveDetectionOn());
}

// ---------------------------------------------------------------------------
// Drop-in and the source strip (UI.md §16.2)
// ---------------------------------------------------------------------------

TEST_CASE ("A file dragged over the window is accepted or refused", "[unit][ui][source]")
{
    Fixture f;
    auto& visualiser = f.editor->getVisualiser();

    CHECK (f.editor->isInterestedInFileDrag ({"C:/loops/a.wav"}));
    CHECK_FALSE (f.editor->isInterestedInFileDrag ({"a.wav", "b.wav"}));

    f.editor->fileDragEnter ({f.temp.folder.getChildFile ("Loop.MP3").getFullPathName()}, 10, 10);
    CHECK (visualiser.getDropHint() == DropHint::supported);
    f.editor->fileDragExit ({});
    CHECK (visualiser.getDropHint() == DropHint::none);

    const auto text = f.temp.folder.getChildFile ("notes.txt");
    f.editor->fileDragEnter ({text.getFullPathName()}, 10, 10);
    CHECK (visualiser.getDropHint() == DropHint::unsupported);
    f.editor->filesDropped ({text.getFullPathName()}, 10, 10);
    CHECK (visualiser.getDropHint() == DropHint::none);
    CHECK (f.editor->getToast().isVisible());
    CHECK (f.editor->getToast().getText().contains ("isn't supported"));
    CHECK (f.processor.getAnalysisWorker().getSourceStatus().phase == AnalysisWorker::SourcePhase::None);
}

TEST_CASE ("A dropped file becomes the source, shown with its key", "[unit][ui][source][key]")
{
    Fixture f;
    const auto file = writeTune (f.temp.folder, "riff.wav");
    f.editor->filesDropped ({file.getFullPathName()}, 10, 10);

    REQUIRE (waitFor (
        [&] { return f.processor.getAnalysisWorker().getSourceStatus().phase == AnalysisWorker::SourcePhase::Ready; }));
    f.editor->refreshSource();

    auto& strip = f.editor->getVisualiser().getSourceStrip();
    CHECK (strip.isVisible());
    CHECK (strip.getCaption().startsWith ("riff.wav"));
    CHECK (strip.getCaption().contains ("D minor"));
    CHECK (strip.getBounds().getHeight() == ui::theme::visualiser::sourceStripHeight);

    // The badge reads the source's key, not the input's.
    const auto& shown = f.editor->getTopBar().getKeyBadge().getShown();
    CHECK (shown.from == "riff.wav");
    CHECK (shown.estimate.root == params::KeyRoot::D);

    // ✕ removes it.
    strip.getRemoveButton().onClick();
    CHECK_FALSE (strip.isVisible());
    CHECK (f.processor.getAnalysisWorker().getSourceRef().isEmpty());
    CHECK (f.editor->getTopBar().getKeyBadge().getShown().from == "the input");
}

TEST_CASE ("A missing source file offers Locate and Dismiss, without blocking", "[unit][ui][source]")
{
    Fixture f;

    // A project whose source file has gone, with no copy saved.
    juce::MemoryBlock saved;
    {
        ThunderProcessor other;
        other.getStateInformation (saved);
    }
    auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), static_cast<int> (saved.getSize()));
    REQUIRE (xml != nullptr);
    auto tree = juce::ValueTree::fromXml (*xml);
    source::SourceRef ref;
    ref.path = f.temp.folder.getChildFile ("gone.wav").getFullPathName();
    tree.getChildWithName (state::ids::session).appendChild (ref.toValueTree(), nullptr);
    juce::MemoryBlock project;
    juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), project);

    f.processor.setStateInformation (project.getData(), static_cast<int> (project.getSize()));
    REQUIRE (waitFor (
        [&]
        { return f.processor.getAnalysisWorker().getSourceStatus().phase == AnalysisWorker::SourcePhase::Failed; }));
    f.editor->refreshSource();

    auto& toast = f.editor->getToast();
    CHECK (toast.isVisible());
    CHECK (toast.getText() == "The source file couldn't be found: gone.wav");
    REQUIRE (toast.getNumActions() == 2);
    CHECK (toast.getActionButton (0).getButtonText() == juce::String::fromUTF8 ("Locate\xe2\x80\xa6"));
    CHECK (toast.getActionButton (1).getButtonText() == "Dismiss");
    CHECK (toast.getHeight() == ui::theme::size::toastActionsHeight);

    // Shown once, not every frame.
    toast.getActionButton (1).onClick();
    CHECK_FALSE (toast.isVisible());
    f.editor->refreshSource();
    CHECK_FALSE (toast.isVisible());

    // The project still remembers the file.
    CHECK (f.processor.getAnalysisWorker().getSourceRef().path == ref.path);
    CHECK (f.editor->getVisualiser().getSourceStrip().getCaption().contains ("gone.wav"));
}

TEST_CASE ("The source strip draws a captured source's bars at its own tempo", "[unit][ui][source]")
{
    auto buffer = std::make_shared<source::SourceBuffer>();
    buffer->sampleRate = 48000.0;
    buffer->audio.setSize (2, 4 * 96000); // 4 bars at 120 BPM
    buffer->audio.clear();
    buffer->fromCapture = true;
    buffer->bpm = 120.0;
    buffer->bars = 4;
    buffer->name = "Thunder_Input.wav";
    buffer->peaks.assign (source::SourceBuffer::kPeakCount, 0.1f);

    AnalysisWorker::SourceStatus status;
    status.phase = AnalysisWorker::SourcePhase::Ready;
    status.name = buffer->name;
    status.buffer = buffer;

    ui::SourceStrip strip;
    strip.setBounds (0, 0, 900, 40);
    strip.setHostTempo (90.0, 3, 4); // the host's tempo doesn't matter for a capture
    strip.setStatus (status);

    const auto lines = strip.barLinePositions();
    REQUIRE (lines.size() == 4);
    const auto spacing = lines[1] - lines[0];
    CHECK (std::abs (spacing - static_cast<float> (strip.getWaveArea().getWidth()) / 4.0f) < 0.5f);
    CHECK (strip.getCaption().contains ("4 bars"));

    // A dropped file follows the host's tempo.
    buffer->fromCapture = false;
    buffer->bpm = 0.0;
    strip.setHostTempo (60.0, 4, 4); // a bar is 4 s: one line, at the start (8 s long)
    strip.setStatus (status);
    CHECK (strip.barLinePositions().size() == 2);
}

// ---------------------------------------------------------------------------
// The capture bar (UI.md §8.6, §9.10; FR-CAPT-004, FR-INPUT-003)
// ---------------------------------------------------------------------------

TEST_CASE ("The capture length menu holds Include tail and Loop", "[unit][ui][capture]")
{
    Fixture f;
    auto& widget = f.editor->getCaptureWidget();
    CHECK (widget.getLengthText() == "4 bars");

    auto menu = widget.buildLengthMenu();
    REQUIRE (findItem (menu, ui::CaptureWidget::kMenuTail).has_value());
    CHECK_FALSE (findItem (menu, ui::CaptureWidget::kMenuTail)->isTicked);
    CHECK (findItem (menu, 4)->isTicked);

    widget.chooseLengthMenuItem (ui::CaptureWidget::kMenuLoop); // Loop brings the tail
    CHECK (f.processor.getSessionState().captureLoop);
    CHECK (f.processor.getSessionState().captureTail);
    CHECK (widget.getLengthText() == "4 bars loop");

    widget.chooseLengthMenuItem (ui::CaptureWidget::kMenuTail); // no tail, no loop
    CHECK_FALSE (f.processor.getSessionState().captureTail);
    CHECK_FALSE (f.processor.getSessionState().captureLoop);

    widget.chooseLengthMenuItem (ui::CaptureWidget::kMenuTail);
    CHECK (widget.getLengthText() == "4 bars +tail");
    widget.chooseLengthMenuItem (2);
    CHECK (f.processor.getSessionState().captureBars == 2);
    CHECK (widget.getLengthText() == "2 bars +tail");

    // Input captures have no tail.
    widget.chooseSource (state::CaptureSource::Input);
    CHECK (f.processor.getSessionState().captureSource == state::CaptureSource::Input);
    CHECK (widget.getLengthText() == "2 bars");
    menu = widget.buildLengthMenu();
    CHECK_FALSE (findItem (menu, ui::CaptureWidget::kMenuTail)->isEnabled);
    CHECK_FALSE (findItem (menu, ui::CaptureWidget::kMenuLoop)->isEnabled);
}

TEST_CASE ("Capture options are saved with the project", "[unit][ui][capture][state]")
{
    juce::MemoryBlock saved;
    {
        ThunderProcessor processor;
        auto session = processor.getSessionState();
        session.captureSource = state::CaptureSource::Input;
        session.captureTail = true;
        session.captureLoop = true;
        processor.setSessionState (session);
        processor.getStateInformation (saved);
    }
    ThunderProcessor reopened;
    reopened.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    const auto s = reopened.getSessionState();
    CHECK (s.captureSource == state::CaptureSource::Input);
    CHECK (s.captureTail);
    CHECK (s.captureLoop);
    CHECK (s.keyAutoDetect);
}
