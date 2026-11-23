#include "SourceRef.h"

#include "SourceLoader.h"
#include "core/state/StateIds.h"

namespace thunder::source
{

namespace ids = state::ids;

bool SourceRef::sameSourceAs (const SourceRef& other) const noexcept
{
    const auto sameCopy = (embedded == nullptr) == (other.embedded == nullptr) &&
                          (embedded == nullptr || embedded == other.embedded || *embedded == *other.embedded);
    return path == other.path && gridOffsetSamples == other.gridOffsetSamples && fromCapture == other.fromCapture &&
           sameCopy;
}

juce::ValueTree SourceRef::toValueTree() const
{
    if (isEmpty())
        return {};

    juce::ValueTree tree{ids::source};
    tree.setProperty (ids::sourcePath, path, nullptr);
    tree.setProperty (ids::sourceGridOffset, static_cast<juce::int64> (gridOffsetSamples), nullptr);
    tree.setProperty (ids::sourceCaptured, fromCapture, nullptr);
    if (bpm > 0.0)
    {
        tree.setProperty (ids::sourceBpm, bpm, nullptr);
        tree.setProperty (ids::sourceBars, bars, nullptr);
        tree.setProperty (ids::sourceTimeSigNumerator, timeSigNumerator, nullptr);
        tree.setProperty (ids::sourceTimeSigDenominator, timeSigDenominator, nullptr);
    }
    if (embedded != nullptr)
        tree.setProperty (ids::sourceEmbedded, embedded->toBase64Encoding(), nullptr);
    return tree;
}

SourceRef SourceRef::fromValueTree (const juce::ValueTree& tree)
{
    SourceRef ref;
    if (!tree.hasType (ids::source))
        return ref;

    ref.path = tree.getProperty (ids::sourcePath).toString();
    ref.gridOffsetSamples =
        std::max<juce::int64> (0, static_cast<juce::int64> (tree.getProperty (ids::sourceGridOffset, 0)));
    ref.fromCapture = static_cast<bool> (tree.getProperty (ids::sourceCaptured, false));

    if (const auto savedBpm = static_cast<double> (tree.getProperty (ids::sourceBpm, 0.0));
        savedBpm > 0.0 && savedBpm < 1000.0)
    {
        ref.bpm = savedBpm;
        ref.bars = juce::jlimit (0, 1024, static_cast<int> (tree.getProperty (ids::sourceBars, 0)));
        ref.timeSigNumerator =
            juce::jlimit (1, 32, static_cast<int> (tree.getProperty (ids::sourceTimeSigNumerator, 4)));
        ref.timeSigDenominator =
            juce::jlimit (1, 32, static_cast<int> (tree.getProperty (ids::sourceTimeSigDenominator, 4)));
    }

    // A copy far larger than Thunder writes isn't one of its own: ignore it.
    if (const auto text = tree.getProperty (ids::sourceEmbedded).toString();
        text.isNotEmpty() && static_cast<std::size_t> (text.length()) <= kMaxEmbeddedBytes * 2u)
    {
        auto block = std::make_shared<juce::MemoryBlock>();
        if (block->fromBase64Encoding (text) && !block->isEmpty())
            ref.embedded = std::move (block);
    }

    return ref;
}

} // namespace thunder::source
