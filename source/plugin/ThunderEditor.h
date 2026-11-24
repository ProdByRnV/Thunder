#pragma once

#include "core/state/UserSettings.h"
#include "ui/components/CaptureWidget.h"
#include "ui/overlays/AboutPanel.h"
#include "ui/overlays/PresetBrowser.h"
#include "ui/overlays/SaveForm.h"
#include "ui/overlays/SettingsPanel.h"
#include "ui/regions/ChainRail.h"
#include "ui/regions/ModulePanels.h"
#include "ui/regions/OutputPanel.h"
#include "ui/regions/TopBar.h"
#include "ui/regions/VisualiserRegion.h"
#include "ui/theme/ThunderLookAndFeel.h"
#include "ui/theme/ThunderPaint.h"
#include "ui/visualiser/VisualModel.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_opengl/juce_opengl.h>

#include <memory>
#include <optional>
#include <vector>

namespace thunder
{

class ThunderProcessor;

// Thunder's editor (UI.md §7–§10; PRD UI-GEN-001/002, UI-FX-004):
//
//   top bar      logo · presets · key · settings · about
//   visualiser   waveform or spectrum, live (UI.md §8.2)
//   chain        IN · Pitch · Grains · Space · Tone · Width · OUT
//   module       the selected module's controls      │ output
//   bottom bar   capture                              status
//
// Everything is laid out at the base size (1040 × 680) and scaled as one
// (75–200%, fixed aspect ratio; UI.md §7.1). Dragging the corner snaps to 5%
// steps; Settings offers 75, 90, 100, 125, 150, 175 and 200%. The size is
// remembered for the project and as the default for new ones. A window that
// wouldn't fit the screen opens at the largest size that does (UI.md UQ-03).
//
// On Windows and macOS rendering goes through an OpenGL context (PRD DEC-004);
// on Linux, JUCE's normal renderer draws the same thing (DEC-048). The editor
// only talks to the processor through parameters, the preset manager, the
// session state and the capture API.
class ThunderEditor final
    : public juce::AudioProcessorEditor
    , public juce::FileDragAndDropTarget
    , private juce::Timer
{
public:
    explicit ThunderEditor (ThunderProcessor& processor);
    ~ThunderEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    // [MSG] For the regions and for tests.
    void selectModule (int index);
    int getSelectedModule() const noexcept { return chain.getSelected(); }
    ui::ModulePanel& getModulePanel (int index) { return *modules[static_cast<std::size_t> (index)]; }
    int getNumModules() const noexcept { return static_cast<int> (modules.size()); }
    ui::ChainRail& getChain() noexcept { return chain; }
    ui::TopBar& getTopBar() noexcept { return topBar; }
    ui::OutputPanel& getOutputPanel() noexcept { return output; }
    ui::VisualiserRegion& getVisualiser() noexcept { return visualiser; }
    ui::VisualModel& getVisualModel() noexcept { return visuals; }
    ui::TonePanel& getTonePanel() noexcept { return *tonePanel; }
    ui::CaptureWidget& getCaptureWidget() noexcept { return captureWidget; }
    ui::SaveForm* getSaveForm() noexcept { return saveForm.get(); }
    ui::PresetBrowser* getPresetBrowser() noexcept { return presetBrowser.get(); }
    ui::SettingsPanel* getSettingsPanel() noexcept { return settingsPanel.get(); }
    int getFrameRate() const noexcept { return frameRate(); } // animation updates a second
    ui::AboutPanel* getAboutPanel() noexcept { return aboutPanel.get(); }
    ui::Toast& getToast() noexcept { return toast; }
    juce::Component& getContent() noexcept { return content; }

    void openSaveForm (bool saveAs);
    void openPresetBrowser();
    void openSettings();
    void openAbout();
    void showMessage (const juce::String& text, bool isError);

    // [MSG] An audio file dragged anywhere over the window can be dropped as the
    // source (UI.md §16.2, PRD FR-INPUT-001).
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragMove (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    // [MSG] The source strip, its messages and the detected key, from the
    // analysis worker. The timer calls it; tests call it directly.
    void refreshSource();

    // [MSG] "Locate…": choose the source file again.
    void locateSource();

    // [MSG] Shows the spectrum view (and remembers it for the project).
    void showSpectrum();

    // [MSG] One frame of the visualiser and meters (PRD UI-VIS-006): reads what
    // the audio thread sent and repaints what changed. While the window isn't
    // showing, nothing is read and the audio thread skips the visual work. The
    // frame timer calls it 60 times a second; tests call it directly.
    void updateVisuals (bool showing, double seconds);

    // [MSG] Morph (UI.md §9.17): whether there is a sound B, and the second
    // ring on every knob showing what Morph sends. The timer calls it; tests too.
    void updateMorph();

    // [MSG] Sound B from the current sound, from a preset, or none.
    void setSoundBFromCurrent();
    void setSoundBFromPreset (const presets::Entry& entry);
    void clearSoundB();
    const std::vector<ui::ThunderKnob*>& getKnobs() const noexcept { return knobs; }

    // [MSG] The window at `scale` (snapped to 5%, 75–200%), remembered for the
    // project and for new ones.
    void setScale (float scale);
    float getScale() const noexcept { return scale; }

    bool areTooltipsShown() const noexcept { return tooltips != nullptr; }
    // Whether this platform draws through OpenGL (Windows, macOS), and whether
    // the context is set up for this window (it starts when the window shows).
#if JUCE_LINUX || JUCE_BSD
    static constexpr bool useOpenGL = false;
#else
    static constexpr bool useOpenGL = true;
#endif
    bool usesOpenGL() const noexcept { return openGL.getTargetComponent() == this; }

    // The nearest 5% step in 75–200%.
    static float snapScale (float scale) noexcept;

    // The scale to open at: `wanted`, or less if the window (plus room for the
    // host's frame) wouldn't fit `workArea`. An empty work area (no display)
    // leaves `wanted` as it is.
    static float fittingScale (float wanted, juce::Rectangle<int> workArea) noexcept;

private:
    // Everything at base size; the editor scales it.
    class Content final : public juce::Component
    {
    public:
        explicit Content (ThunderEditor& owner);
        void paint (juce::Graphics& g) override;
        void resized() override;

    private:
        void paintStatic (juce::Graphics& g);

        ThunderEditor& owner;
        ui::paint::CachedLayer staticLayer;
    };

    // Keeps the aspect ratio and the 75–200% range, and snaps to 5% steps
    // (for the corner and for hosts that resize the window).
    class ScaleConstrainer final : public juce::ComponentBoundsConstrainer
    {
    public:
        void checkBounds (juce::Rectangle<int>& bounds, const juce::Rectangle<int>& previous,
                          const juce::Rectangle<int>& limits, bool stretchingTop, bool stretchingLeft,
                          bool stretchingBottom, bool stretchingRight) override;
    };

    void timerCallback() override;
    void applyQuality(); // the latency shown, the frame rate and the spectrum's detail
    int frameRate() const noexcept;
    void applyMotion (const state::UserSettings& settings); // Reduce Motion and the frame rate
    void showTooltips (bool show);
    void rememberScale();
    void rememberView (ui::VisualiserRegion::View view);

    ThunderProcessor& thunderProcessor;
    ui::ThunderLookAndFeel lookAndFeel;
    juce::OpenGLContext openGL;
    ScaleConstrainer scaleConstrainer;
    ui::VisualModel visuals; // before the regions that read it

    Content content;
    ui::TopBar topBar;
    ui::VisualiserRegion visualiser;
    ui::ChainRail chain;
    std::vector<std::unique_ptr<ui::ModulePanel>> modules;
    ui::OutputPanel output;
    ui::CaptureWidget captureWidget;
    juce::Label status;
    ui::Toast toast;
    std::unique_ptr<ui::SaveForm> saveForm;
    std::unique_ptr<ui::PresetBrowser> presetBrowser;
    std::unique_ptr<ui::SettingsPanel> settingsPanel;
    std::unique_ptr<ui::AboutPanel> aboutPanel;
    std::unique_ptr<juce::TooltipWindow> tooltips;
    ui::TonePanel* tonePanel = nullptr;
    int frameRateSetting = 60; // Settings: 60 or 30
    double lastFrameMs = 0.0;
    float scale = 1.0f;
    bool constructed = false;
    std::uint32_t seenSourceVersion = 0;
    std::unique_ptr<juce::FileChooser> sourceChooser;
    std::vector<ui::ThunderKnob*> knobs; // every knob, for Morph's second ring
    std::optional<params::ParamSnapshot> shownMorph;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThunderEditor)
};

} // namespace thunder
