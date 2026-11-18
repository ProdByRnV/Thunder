// Smoothing, crossfades, the lock-free queue and sample sanitising.

#include "core/util/Crossfade.h"
#include "core/util/Math.h"
#include "core/util/Smoother.h"
#include "core/util/SpscFifo.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <limits>
#include <thread>
#include <vector>

using namespace thunder::util;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

TEST_CASE ("A linear smoother ramps to its target in the set time", "[unit][util]")
{
    LinearSmoother s;
    s.prepare (1000.0, 0.010); // 10 samples
    s.snapTo (0.0f);
    s.setTarget (1.0f);

    CHECK (s.isSmoothing());
    for (int i = 0; i < 5; ++i)
        s.next();
    CHECK_THAT (s.current(), WithinAbs (0.5, 1e-6));

    CHECK_THAT (s.skip (5), WithinAbs (1.0, 1e-6));
    CHECK_FALSE (s.isSmoothing());
}

TEST_CASE ("A frequency smoother ramps in equal ratios", "[unit][util]")
{
    FrequencySmoother s;
    s.prepare (1000.0, 0.010);
    s.snapTo (100.0f);
    s.setTarget (400.0f);

    s.skip (5);
    CHECK_THAT (s.current(), WithinRel (200.0f, 1e-4f)); // halfway in ratio, not 250
}

TEST_CASE ("A gain smoother changes level without steps", "[unit][util]")
{
    constexpr int rampSamples = 480;
    GainSmoother g;
    g.prepare (48000.0, rampSamples / 48000.0);
    g.snapToDecibels (0.0f);
    g.setTargetDecibels (-24.0f);

    std::vector<float> data (2000, 1.0f);
    float* channels[] = {data.data()};
    g.apply (channels, 1, static_cast<int> (data.size()));

    const auto target = juce::Decibels::decibelsToGain (-24.0f);
    // 1% allowance for float rounding as the ramp accumulates.
    const auto maxStep = (1.0f - target) / rampSamples * 1.01f;

    for (std::size_t i = 1; i < data.size(); ++i)
        REQUIRE (std::abs (data[i] - data[i - 1]) <= maxStep);

    CHECK_THAT (data.back(), WithinAbs (target, 1e-6));
}

TEST_CASE ("A gain smoother at 0 dB leaves audio untouched", "[unit][util]")
{
    GainSmoother g;
    g.prepare (48000.0);
    g.snapToDecibels (0.0f);

    std::vector<float> data{0.1f, -0.7f, 0.33f};
    const auto original = data;
    float* channels[] = {data.data()};
    g.apply (channels, 1, 3);

    CHECK (data == original);
}

TEST_CASE ("Crossfade gains keep the level steady", "[unit][util]")
{
    Crossfade equalPower;
    equalPower.prepare (48000.0, kBypassFadeSeconds, FadeLaw::EqualPower);

    Crossfade linear;
    linear.prepare (48000.0, kBypassFadeSeconds, FadeLaw::Linear);

    for (int step = 0; step <= 10; ++step)
    {
        const auto p = static_cast<float> (step) / 10.0f;

        const auto [a, b] = equalPower.gainsAt (p);
        CHECK_THAT (a * a + b * b, WithinAbs (1.0, 1e-6)); // constant power

        const auto [la, lb] = linear.gainsAt (p);
        CHECK_THAT (la + lb, WithinAbs (1.0, 1e-6)); // constant amplitude
    }

    const auto [midA, midB] = equalPower.gainsAt (0.5f);
    CHECK_THAT (midA, WithinAbs (std::sqrt (0.5), 1e-6));
    CHECK_THAT (midB, WithinAbs (std::sqrt (0.5), 1e-6));
}

TEST_CASE ("A crossfade moves from A to B and back without jumps", "[unit][util]")
{
    Crossfade fade;
    fade.prepare (1000.0, 0.010, FadeLaw::Linear); // 10 samples
    CHECK (fade.isFullyA());

    std::vector<float> a (10, 1.0f), b (10, 0.0f);
    float* aPtr[] = {a.data()};
    const float* bPtr[] = {b.data()};

    // Fully at A: A passes untouched.
    fade.process (aPtr, bPtr, 1, 10);
    CHECK_THAT (a[9], WithinAbs (1.0, 1e-6));

    // Five samples towards B, then reverse: the position continues from 0.5.
    fade.setTarget (true);
    fade.process (aPtr, bPtr, 1, 5);
    CHECK (fade.isFading());
    CHECK_THAT (fade.getPosition(), WithinAbs (0.5, 1e-6));
    CHECK_THAT (a[4], WithinAbs (0.5, 1e-6));

    fade.setTarget (false);
    std::fill (a.begin(), a.end(), 1.0f);
    fade.process (aPtr, bPtr, 1, 1);
    CHECK_THAT (a[0], WithinAbs (0.6, 1e-6));

    // All the way to B: B is copied through.
    fade.setTarget (true);
    std::fill (a.begin(), a.end(), 1.0f);
    fade.process (aPtr, bPtr, 1, 10);
    CHECK (fade.isFullyB());
    std::fill (a.begin(), a.end(), 1.0f);
    fade.process (aPtr, bPtr, 1, 10);
    CHECK_THAT (a[0], WithinAbs (0.0, 1e-9));

    fade.snapTo (false);
    CHECK (fade.isFullyA());
}

TEST_CASE ("The FIFO keeps order, wraps around and counts what it drops", "[unit][util]")
{
    SpscFifo<int> fifo (8);
    CHECK (fifo.getCapacity() == 8);

    const int first[] = {1, 2, 3, 4, 5};
    CHECK (fifo.push (first, 5) == 5);

    int out[16] = {};
    CHECK (fifo.pop (out, 3) == 3);
    CHECK ((out[0] == 1 && out[1] == 2 && out[2] == 3));

    // 2 items queued, room for 6: 4 of these 10 are dropped.
    const int second[] = {6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    CHECK (fifo.push (second, 10) == 6);
    CHECK (fifo.getDroppedCount() == 4u);
    CHECK (fifo.getNumReady() == 8);

    CHECK (fifo.pop (out, 16) == 8);
    for (int i = 0; i < 8; ++i)
        CHECK (out[i] == 4 + i);

    fifo.clear();
    CHECK (fifo.getNumReady() == 0);
    CHECK (fifo.getDroppedCount() == 0u);
}

TEST_CASE ("The FIFO is safe between two threads", "[unit][util]")
{
    constexpr int total = 200000;
    SpscFifo<int> fifo (64);

    std::thread producer (
        [&]
        {
            for (int next = 0; next < total;)
            {
                if (fifo.push (&next, 1) == 1)
                    ++next;
                else
                    std::this_thread::yield();
            }
        });

    int expected = 0;
    bool inOrder = true;
    while (expected < total)
    {
        int value = -1;
        if (fifo.pop (&value, 1) == 1)
        {
            inOrder = inOrder && value == expected;
            ++expected;
        }
        else
        {
            std::this_thread::yield();
        }
    }

    producer.join();
    CHECK (inOrder);
}

TEST_CASE ("Non-finite samples are replaced with silence", "[unit][util]")
{
    constexpr auto inf = std::numeric_limits<float>::infinity();
    std::vector<float> data{0.5f, std::numeric_limits<float>::quiet_NaN(), inf,
                            -inf, std::numeric_limits<float>::max(),       std::numeric_limits<float>::denorm_min()};

    CHECK (replaceNonFinite (data.data(), static_cast<int> (data.size())) == 3);

    const std::vector<float> expected{
        0.5f, 0.0f, 0.0f, 0.0f, std::numeric_limits<float>::max(), std::numeric_limits<float>::denorm_min()};
    CHECK (data == expected);
}
