#pragma once

#include <array>
#include <atomic>
#include <type_traits>

// Latest-value hand-over from one writer thread to one reader thread
// (ARCHITECTURE §6.3, pointer swap): the writer never waits and never fails,
// the reader never waits and always sees a complete value, and a value the
// reader didn't get to in time is simply replaced by a newer one.
//
// Three slots: the writer fills its own back slot and swaps it with the middle
// one, marking it new; the reader, when the middle slot is new, swaps it with
// its own front slot. Neither side ever touches the other's slot.
//
// Used for Morph's sound B (PRD FR-PLAY-002): the message thread sets it, the
// audio thread reads it at the start of each block.

namespace thunder::util
{

template<typename T> class TripleBuffer
{
    static_assert (std::is_trivially_copyable_v<T>, "TripleBuffer copies whole values");

public:
    // [writer] Hands `value` over. Only one thread may write.
    void write (const T& value) noexcept
    {
        slots[static_cast<std::size_t> (back)] = value;
        back = middle.exchange (back | kNew, std::memory_order_acq_rel) & kIndex;
    }

    // [reader] Takes the newest value, if one arrived since the last call.
    // Returns true when get() changed.
    bool update() noexcept
    {
        if ((middle.load (std::memory_order_acquire) & kNew) == 0)
            return false;

        front = middle.exchange (front, std::memory_order_acq_rel) & kIndex;
        return true;
    }

    // [reader] The value taken by the last update() (a default T before any).
    const T& get() const noexcept { return slots[static_cast<std::size_t> (front)]; }

private:
    static constexpr int kIndex = 3;
    static constexpr int kNew = 4;

    std::array<T, 3> slots{};
    std::atomic<int> middle{1};
    int back = 0;  // the writer's
    int front = 2; // the reader's
};

} // namespace thunder::util
