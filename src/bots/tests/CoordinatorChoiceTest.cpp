#include "CoordinatorChoice.h"

#include <gtest/gtest.h>

using Bots::ChooseCoordinator;
using Bots::CoordinatorChoice;
using Bots::LeaderPlace;
using Bots::MapPlace;

namespace
{
    MapPlace const Orgrimmar = {1, 0};
    MapPlace const RagefireChasm = {389, 7};
    MapPlace const OtherRagefireChasm = {389, 8};
}

TEST(CoordinatorChoice, ABotWithoutAGroupUsesItsOwnCoordinator)
{
    EXPECT_EQ(ChooseCoordinator(false, Orgrimmar, LeaderPlace::Unknown()), CoordinatorChoice::Own);
}

TEST(CoordinatorChoice, ABotOnTheMapOfTheLeaderUsesTheGroupCoordinator)
{
    EXPECT_EQ(ChooseCoordinator(true, RagefireChasm, LeaderPlace::On(RagefireChasm)), CoordinatorChoice::Group);
}

TEST(CoordinatorChoice, ABotOnAnotherMapThanTheLeaderIgnoresTheGroupCoordinator)
{
    // The leader entered the instance first: the bot is still outside.
    EXPECT_EQ(ChooseCoordinator(true, Orgrimmar, LeaderPlace::On(RagefireChasm)), CoordinatorChoice::Own);
}

TEST(CoordinatorChoice, ABotInAnotherInstanceOfTheMapIgnoresTheGroupCoordinator)
{
    EXPECT_EQ(ChooseCoordinator(true, OtherRagefireChasm, LeaderPlace::On(RagefireChasm)), CoordinatorChoice::Own);
}

TEST(CoordinatorChoice, ABotUsesTheGroupCoordinatorAgainWhenItReachesTheMapOfTheLeader)
{
    LeaderPlace const leader = LeaderPlace::On(RagefireChasm);
    ASSERT_EQ(ChooseCoordinator(true, Orgrimmar, leader), CoordinatorChoice::Own);

    EXPECT_EQ(ChooseCoordinator(true, RagefireChasm, leader), CoordinatorChoice::Group);
}

TEST(CoordinatorChoice, ABotUsesTheGroupCoordinatorWhenTheLeaderIsOffline)
{
    // No map to compare: the bot keeps the group's state.
    EXPECT_EQ(ChooseCoordinator(true, Orgrimmar, LeaderPlace::Unknown()), CoordinatorChoice::Group);
}
