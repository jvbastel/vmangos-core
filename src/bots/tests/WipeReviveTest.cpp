#include "WipeRevive.h"

#include <gtest/gtest.h>

using Bots::RunMember;
using Bots::WipeWatch;

namespace
{
    RunMember const Living = RunMember::Living;
    RunMember const Dead = RunMember::Dead;
    RunMember const InTeleport = RunMember::InTeleport;
    RunMember const NotInWorld = RunMember::NotInWorld;

    // The watch after `ms` of the same members, from the time 1000.
    bool After(WipeWatch& watch, std::vector<RunMember> const& members, std::int64_t ms)
    {
        watch.Update(members, 1000);
        return watch.Update(members, 1000 + ms);
    }
}

TEST(WipeRevive, EveryMemberDeadRevivesAfterTheDelay)
{
    WipeWatch watch;
    std::vector<RunMember> const members = {Dead, Dead, Dead};

    EXPECT_FALSE(After(watch, members, Bots::WipeReviveDelayMs - 1));
    EXPECT_TRUE(watch.Update(members, 1000 + Bots::WipeReviveDelayMs));
}

TEST(WipeRevive, ALivingMemberIsNoWipe)
{
    WipeWatch watch;

    EXPECT_FALSE(After(watch, {Dead, Living, Dead}, 60000));
}

// A member that logged out, or is not in the world: it does not hold the
// others dead. They revive without it.
TEST(WipeRevive, AMemberNotInTheWorldDoesNotBlockTheWipe)
{
    WipeWatch watch;

    EXPECT_TRUE(After(watch, {Dead, NotInWorld, Dead}, Bots::WipeReviveDelayMs));
}

TEST(WipeRevive, WithNoMemberInTheWorldNobodyRevives)
{
    WipeWatch watch;

    EXPECT_FALSE(After(watch, {NotInWorld, NotInWorld}, 60000));
}

// A member in a teleport can land alive: the wipe waits for it, and the
// delay starts again when it lands dead.
TEST(WipeRevive, AMemberInATeleportHoldsTheWipe)
{
    WipeWatch watch;
    EXPECT_FALSE(After(watch, {Dead, InTeleport, Dead}, 60000));

    EXPECT_FALSE(watch.Update({Dead, Dead, Dead}, 70000));
    EXPECT_FALSE(watch.Update({Dead, Dead, Dead}, 70000 + Bots::WipeReviveDelayMs - 1));
    EXPECT_TRUE(watch.Update({Dead, Dead, Dead}, 70000 + Bots::WipeReviveDelayMs));
}

TEST(WipeRevive, AfterTheReviveTheDelayStartsAgain)
{
    WipeWatch watch;
    std::vector<RunMember> const members = {Dead, Dead};
    ASSERT_TRUE(After(watch, members, Bots::WipeReviveDelayMs));

    EXPECT_FALSE(watch.Update(members, 1000 + Bots::WipeReviveDelayMs + 1));
}
