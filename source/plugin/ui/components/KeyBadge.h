#pragma once

#include "core/analysis/KeyDetector.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace thunder::ui
{

// The detected key beside C-KEYSELECT (UI.md §9.13, PRD FR-KEY-003): a dot and
// the confidence, "● 87%". The dot is cyan from 70%, amber from 40%, grey
// below that or when nothing has been detected. Clicking it opens the key menu
// (TopBar), where the detected key can be used; the badge itself never
// changes the key.
class KeyBadge final : public juce::Button
{
public:
    struct Shown
    {
        analysis::KeyEstimate estimate;
        juce::String from;      // "the input", or the source's name
        bool listening = false; // Auto Detect is on (or a source was analysed)

        bool operator== (const Shown& other) const noexcept
        {
            return estimate.sameKeyAs (other.estimate) &&
                   juce::approximatelyEqual (estimate.confidence, other.estimate.confidence) && from == other.from &&
                   listening == other.listening;
        }
    };

    static constexpr float kConfident = 0.7f;
    static constexpr float kUnsure = 0.4f;

    KeyBadge();

    void setShown (const Shown& shown);
    const Shown& getShown() const noexcept { return shown; }

    // What it shows, for tests and screen readers.
    juce::String getBadgeText() const;
    juce::Colour getDotColour() const;

    // "A minor", or empty when nothing is detected.
    static juce::String keyName (const analysis::KeyEstimate& estimate);

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override;

private:
    void describe();

    Shown shown;
};

} // namespace thunder::ui
