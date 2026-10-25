#include "StartDelay.h"

#include <gtest/gtest.h>

#include <vector>

using Bots::DrawStartDelay;
using Bots::RandomSource;
using Bots::StartDelaySettings;

namespace
{
    // A random source that gives back fixed numbers and keeps the limits of
    // each draw.
    class FixedRandom : public RandomSource
    {
    public:
        explicit FixedRandom(std::vector<std::uint32_t> values) : m_values(values) {}

        std::uint32_t Between(std::uint32_t low, std::uint32_t high) override
        {
            lows.push_back(low);
            highs.push_back(high);
            std::uint32_t const value = m_values[m_next % m_values.size()];
            ++m_next;
            return value;
        }

        std::vector<std::uint32_t> lows;
        std::vector<std::uint32_t> highs;

    private:
        std::vector<std::uint32_t> m_values;
        std::size_t m_next = 0;
    };
}

TEST(StartDelay, TheDefaultLimitsAre300To1500Ms)
{
    StartDelaySettings const settings;

    EXPECT_EQ(settings.minMs, 300u);
    EXPECT_EQ(settings.maxMs, 1500u);
}

TEST(StartDelay, TheDrawAsksTheRandomSourceForANumberInTheLimits)
{
    FixedRandom random({700});

    EXPECT_EQ(DrawStartDelay({200, 900}, random), 700u);
    EXPECT_EQ(random.lows, (std::vector<std::uint32_t>{200}));
    EXPECT_EQ(random.highs, (std::vector<std::uint32_t>{900}));
}

TEST(StartDelay, ANumberOutsideTheLimitsIsClampedToThem)
{
    FixedRandom random({50, 5000});

    EXPECT_EQ(DrawStartDelay({300, 1500}, random), 300u);
    EXPECT_EQ(DrawStartDelay({300, 1500}, random), 1500u);
}

TEST(StartDelay, AMaximumBelowTheMinimumGivesTheMinimum)
{
    FixedRandom random({100});

    EXPECT_EQ(DrawStartDelay({800, 400}, random), 800u);
    EXPECT_EQ(random.highs, (std::vector<std::uint32_t>{800}));
}

TEST(StartDelay, EqualLimitsGiveThatDelay)
{
    FixedRandom random({0});

    EXPECT_EQ(DrawStartDelay({0, 0}, random), 0u);
}

TEST(StartDelay, TheSeededSourceStaysInTheLimitsAndRepeatsForASeed)
{
    Bots::SeededRandom first(42);
    Bots::SeededRandom second(42);

    for (int i = 0; i < 1000; ++i)
    {
        std::uint32_t const delay = DrawStartDelay({300, 1500}, first);
        EXPECT_GE(delay, 300u);
        EXPECT_LE(delay, 1500u);
        EXPECT_EQ(DrawStartDelay({300, 1500}, second), delay);
    }
}

TEST(StartDelay, TheSeededSourceGivesNewDelays)
{
    Bots::SeededRandom random(7);
    std::uint32_t const first = DrawStartDelay({300, 1500}, random);
    bool changed = false;
    for (int i = 0; i < 20; ++i)
        changed = changed || DrawStartDelay({300, 1500}, random) != first;

    EXPECT_TRUE(changed);
}
