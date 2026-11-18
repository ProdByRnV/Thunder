#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <cstdint>
#include <type_traits>
#include <vector>

// Lock-free single-producer / single-consumer queue (ARCHITECTURE §6.2).
//
// Carries data off the audio thread (visualiser frames, capture audio, the
// analysis feed) without ever blocking it. Exactly one thread pushes and exactly
// one other thread pops. When the queue is full the producer keeps going: what
// doesn't fit is dropped and counted, and the consumer can report the count.

namespace thunder::util
{

static_assert (std::atomic<std::uint64_t>::is_always_lock_free);

template<typename T> class SpscFifo
{
    static_assert (std::is_trivially_copyable_v<T>, "SpscFifo copies items with memcpy-like semantics");

public:
    SpscFifo() = default;

    explicit SpscFifo (int capacity) { setCapacity (capacity); }

    // [NRT] Allocates room for `capacity` items and empties the queue. Neither
    // side may be using the queue while this runs.
    void setCapacity (int capacity)
    {
        jassert (capacity > 0);
        storage.assign (static_cast<std::size_t> (capacity) + 1, T{});
        fifo.setTotalSize (capacity + 1); // AbstractFifo keeps one slot free
        dropped.store (0, std::memory_order_relaxed);
    }

    // [NRT] Empties the queue. Neither side may be using the queue while this runs.
    void clear() noexcept
    {
        fifo.reset();
        dropped.store (0, std::memory_order_relaxed);
    }

    // [producer, RT-safe] Adds up to `count` items; returns how many fitted. The
    // rest are dropped and counted.
    int push (const T* items, int count) noexcept
    {
        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        fifo.prepareToWrite (count, start1, size1, start2, size2);

        copyItems (items, storage.data() + start1, size1);
        copyItems (items + size1, storage.data() + start2, size2);

        const auto written = size1 + size2;
        fifo.finishedWrite (written);

        if (written < count)
            dropped.fetch_add (static_cast<std::uint64_t> (count - written), std::memory_order_relaxed);

        return written;
    }

    // [consumer, RT-safe] Takes up to `count` items; returns how many it took.
    int pop (T* destination, int count) noexcept
    {
        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        fifo.prepareToRead (count, start1, size1, start2, size2);

        copyItems (storage.data() + start1, destination, size1);
        copyItems (storage.data() + start2, destination + size1, size2);

        const auto read = size1 + size2;
        fifo.finishedRead (read);
        return read;
    }

    int getNumReady() const noexcept { return fifo.getNumReady(); }
    int getFreeSpace() const noexcept { return fifo.getFreeSpace(); }
    int getCapacity() const noexcept { return fifo.getTotalSize() - 1; }

    // Items dropped because the queue was full, since setCapacity()/clear().
    std::uint64_t getDroppedCount() const noexcept { return dropped.load (std::memory_order_relaxed); }

private:
    static void copyItems (const T* from, T* to, int count) noexcept
    {
        for (int i = 0; i < count; ++i)
            to[i] = from[i];
    }

    juce::AbstractFifo fifo{1};
    std::vector<T> storage;
    std::atomic<std::uint64_t> dropped{0};

    JUCE_DECLARE_NON_COPYABLE (SpscFifo)
};

} // namespace thunder::util
