#include "SessionState.h"

#include "StateIds.h"

namespace thunder::state
{

namespace
{
constexpr int kValidCaptureBars[] = {1, 2, 4, 8, 16};

const char* toName (CaptureSource source) noexcept
{
    switch (source)
    {
        case CaptureSource::Layer:
            return "layer";
        case CaptureSource::FullMix:
            return "fullMix";
    }

    return "layer";
}
} // namespace

juce::ValueTree SessionState::toValueTree() const
{
    juce::ValueTree tree{ids::session};
    tree.setProperty (ids::presetName, presetName, nullptr);
    tree.setProperty (ids::presetModified, presetModified, nullptr);
    tree.setProperty (ids::presetKey, presetKey, nullptr);
    tree.setProperty (ids::captureBars, captureBars, nullptr);
    tree.setProperty (ids::captureSource, toName (captureSource), nullptr);
    if (uiScale > 0.0f)
        tree.setProperty (ids::uiScale, uiScale, nullptr);
    if (uiView != VisualiserView::notSet)
        tree.setProperty (ids::uiView, uiView == VisualiserView::spectrum ? "spectrum" : "wave", nullptr);
    return tree;
}

SessionState SessionState::fromValueTree (const juce::ValueTree& tree)
{
    SessionState s;

    if (!tree.hasType (ids::session))
        return s;

    s.presetName = tree.getProperty (ids::presetName, s.presetName).toString();
    s.presetModified = static_cast<bool> (tree.getProperty (ids::presetModified, s.presetModified));
    s.presetKey = tree.getProperty (ids::presetKey, s.presetKey).toString();

    const auto bars = static_cast<int> (tree.getProperty (ids::captureBars, s.captureBars));
    for (const auto valid : kValidCaptureBars)
        if (bars == valid)
            s.captureBars = bars;

    // Any scale the editor can show; anything else (or nothing) means "not set".
    if (const auto scale = static_cast<float> (tree.getProperty (ids::uiScale, 0.0f)); scale >= 0.75f && scale <= 2.0f)
        s.uiScale = scale;

    if (tree.getProperty (ids::captureSource).toString() == toName (CaptureSource::FullMix))
        s.captureSource = CaptureSource::FullMix;

    if (const auto view = tree.getProperty (ids::uiView).toString(); view == "wave")
        s.uiView = VisualiserView::wave;
    else if (view == "spectrum")
        s.uiView = VisualiserView::spectrum;

    return s;
}

} // namespace thunder::state
