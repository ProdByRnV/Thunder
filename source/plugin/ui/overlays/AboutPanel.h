#pragma once

#include "plugin/ui/components/Buttons.h"

#include <functional>

namespace thunder::ui
{

// The About panel (UI.md §10.4, PRD REL-LIC-002): the logo, the version and
// build date, the build (plugin format, system, host), what Thunder does, and
// a link for reporting an issue. "Copy details" puts the version and build
// lines on the clipboard for an issue report. Opened by the logo or the ⓘ
// button; Close, Esc, Enter or a click outside closes it.
class AboutPanel final : public juce::Component
{
public:
    // What this build is running as.
    struct Build
    {
        juce::String format; // "VST3", "CLAP", "AU"…
        juce::String host;   // the DAW, when it can be told
    };

    explicit AboutPanel (Build build = {});

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent& e) override;
    bool keyPressed (const juce::KeyPress& key) override;

    std::function<void()> onClose;

    void close();
    juce::Rectangle<int> dialogBounds() const;

    // "Version 0.9.0 · built 24 Nov 2026"
    static juce::String versionLine();

    // "VST3 · Windows 64-bit · FL Studio"
    juce::String buildLine() const;

    // Everything an issue report needs, one item per line.
    juce::String detailsText() const;

    // [MSG] Copies detailsText() to the clipboard.
    void copyDetails();

    static inline const juce::String issuesUrl{"https://github.com/ProdByRnV/Thunder/issues"};

private:
    Build build;
    ThunderButton copyButton{"Copy details", ThunderButton::Variant::ghost};
    ThunderButton reportButton{"Report an issue"};
    ThunderButton closeButton{"Close", ThunderButton::Variant::primary};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AboutPanel)
};

} // namespace thunder::ui
