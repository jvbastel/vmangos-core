#include "HoldWatch.h"

#include <gtest/gtest.h>

using Bots::GroupCoordinator;
using Bots::HoldContactMs;
using Bots::HoldLimitMs;
using Bots::HoldWatch;

namespace
{
    Bots::Guid const Tank = 0x11;
    Bots::Guid const Kobold = 0xF130000302000022;

    std::vector<std::string> Lines(std::initializer_list<char const*> lines)
    {
        return std::vector<std::string>(lines.begin(), lines.end());
    }

    // The tank starts a pull of the kobold from its fight spot, and the
    // kobold attacks it at `engagedMs`.
    void Engage(GroupCoordinator& group, std::int64_t engagedMs)
    {
        group.StartPull(Tank, Kobold);
        group.SetFightSpot(Tank, {1.0f, 2.0f, 3.0f});
        group.HoldPull(Tank, engagedMs);
    }
}

// jvbastel/WOWAI#127: the run trace tells how a held pull opened to the
// damage bots.
TEST(HoldWatch, SaysNothingWithoutAPull)
{
    GroupCoordinator group;
    HoldWatch watch;

    EXPECT_TRUE(watch.Observe(group, 1000).empty());
}

TEST(HoldWatch, TracesTheEngageTheContactTheWayBackAndTheHoldAfterContact)
{
    GroupCoordinator group;
    HoldWatch watch;
    Engage(group, 1000);

    EXPECT_EQ(watch.Observe(group, 1000), Lines({"the tank engages its pull target"}));
    group.NoteFocusContact(2500);
    EXPECT_EQ(watch.Observe(group, 2500), Lines({"the pull target reaches the tank"}));
    group.ClearFightSpot(Tank);
    EXPECT_EQ(watch.Observe(group, 3000), Lines({"the tank is back at its fight spot"}));
    EXPECT_TRUE(watch.Observe(group, 2500 + HoldContactMs - 1).empty());
    EXPECT_EQ(watch.Observe(group, 2500 + HoldContactMs), Lines({"the tank holds threat: melee contact"}));
    EXPECT_TRUE(watch.Observe(group, 1000 + HoldLimitMs).empty());
}

TEST(HoldWatch, TracesTheHoldAfterTheTimeLimit)
{
    GroupCoordinator group;
    HoldWatch watch;
    Engage(group, 1000);
    watch.Observe(group, 1000);

    EXPECT_EQ(watch.Observe(group, 1000 + HoldLimitMs), Lines({"the tank holds threat: time limit"}));
}

TEST(HoldWatch, TracesEachPull)
{
    GroupCoordinator group;
    HoldWatch watch;
    Engage(group, 1000);
    watch.Observe(group, 1000);
    watch.Observe(group, 1000 + HoldLimitMs);
    group.EndPull();
    EXPECT_TRUE(watch.Observe(group, 20000).empty());

    Engage(group, 30000);
    EXPECT_EQ(watch.Observe(group, 30000), Lines({"the tank engages its pull target"}));
}
