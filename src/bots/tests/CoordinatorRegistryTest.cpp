#include "CoordinatorRegistry.h"

#include <gtest/gtest.h>

using Bots::CoordinatorRegistry;
using Bots::GroupEventKind;
using Bots::LeaderPlace;
using Bots::MapPlace;
using Bots::RestReason;

namespace
{
    std::uint32_t const Group = 7;
    std::uint32_t const OtherGroup = 8;
    MapPlace const Orgrimmar = {1, 0};
    MapPlace const RagefireChasm = {389, 7};
    MapPlace const OtherRagefireChasm = {389, 8};
    LeaderPlace const LeaderOutside = LeaderPlace::On(Orgrimmar);
    Bots::Guid const Mage = 0x13;

    bool Exists(std::uint32_t) { return true; }
    bool Gone(std::uint32_t) { return false; }
}

TEST(CoordinatorRegistry, ABotOnTheMapOfTheLeaderGetsTheGroupCoordinator)
{
    CoordinatorRegistry registry;

    EXPECT_EQ(registry.For(Group, Orgrimmar, LeaderOutside, 0), registry.OfGroup(Group));
}

// The leader left the instance: the bots inside still share their state, for
// example the give-up of a resurrect and the tank.
TEST(CoordinatorRegistry, TheBotsOfAGroupAwayFromTheLeaderInOnePlaceShareOneCoordinator)
{
    CoordinatorRegistry registry;

    auto const priest = registry.For(Group, RagefireChasm, LeaderOutside, 0);
    auto const mage = registry.For(Group, RagefireChasm, LeaderOutside, 0);

    EXPECT_EQ(priest, mage);
    EXPECT_NE(priest, registry.OfGroup(Group));
}

TEST(CoordinatorRegistry, BotsInTwoPlacesAwayFromTheLeaderDoNotShare)
{
    CoordinatorRegistry registry;

    EXPECT_NE(registry.For(Group, RagefireChasm, LeaderOutside, 0),
        registry.For(Group, OtherRagefireChasm, LeaderOutside, 0));
}

TEST(CoordinatorRegistry, TwoGroupsInOnePlaceDoNotShare)
{
    CoordinatorRegistry registry;

    EXPECT_NE(registry.For(Group, RagefireChasm, LeaderOutside, 0),
        registry.For(OtherGroup, RagefireChasm, LeaderOutside, 0));
}

TEST(CoordinatorRegistry, AnUnknownLeaderKeepsTheBotsOnTheGroupCoordinator)
{
    CoordinatorRegistry registry;

    EXPECT_EQ(registry.For(Group, RagefireChasm, LeaderPlace::Unknown(), 0), registry.OfGroup(Group));
}

// The leader comes to the bots: they use the group coordinator again, and a
// later time away from the leader starts with no old state.
TEST(CoordinatorRegistry, WhenTheLeaderReachesThePlaceItsCoordinatorEnds)
{
    CoordinatorRegistry registry;
    registry.For(Group, RagefireChasm, LeaderOutside, 0)->GiveUpResurrect(Mage);

    EXPECT_EQ(registry.For(Group, RagefireChasm, LeaderPlace::On(RagefireChasm), 0), registry.OfGroup(Group));
    EXPECT_FALSE(registry.For(Group, RagefireChasm, LeaderOutside, 0)->IsResurrectGivenUp(Mage));
}

TEST(CoordinatorRegistry, APlaceCoordinatorThatNoBotUsesEndsAfterAWhile)
{
    CoordinatorRegistry registry;
    registry.For(Group, RagefireChasm, LeaderOutside, 0)->GiveUpResurrect(Mage);

    registry.DropOld(Exists, CoordinatorRegistry::PlaceIdleMs + 1);

    EXPECT_FALSE(registry.For(Group, RagefireChasm, LeaderOutside, CoordinatorRegistry::PlaceIdleMs + 1)
                     ->IsResurrectGivenUp(Mage));
}

TEST(CoordinatorRegistry, APlaceCoordinatorInUseStays)
{
    CoordinatorRegistry registry;
    registry.For(Group, RagefireChasm, LeaderOutside, 0)->GiveUpResurrect(Mage);
    registry.For(Group, RagefireChasm, LeaderOutside, CoordinatorRegistry::PlaceIdleMs);

    registry.DropOld(Exists, CoordinatorRegistry::PlaceIdleMs + 1);

    EXPECT_TRUE(registry.For(Group, RagefireChasm, LeaderOutside, CoordinatorRegistry::PlaceIdleMs + 1)
                    ->IsResurrectGivenUp(Mage));
}

TEST(CoordinatorRegistry, TheCoordinatorsOfAGroupThatIsGoneEnd)
{
    CoordinatorRegistry registry;
    registry.OfGroup(Group)->GiveUpResurrect(Mage);
    registry.For(Group, RagefireChasm, LeaderOutside, 0)->GiveUpResurrect(Mage);

    registry.DropOld(Gone, 0);

    EXPECT_FALSE(registry.OfGroup(Group)->IsResurrectGivenUp(Mage));
    EXPECT_FALSE(registry.For(Group, RagefireChasm, LeaderOutside, 0)->IsResurrectGivenUp(Mage));
}

TEST(CoordinatorRegistry, DropEndsTheCoordinatorsOfTheGroup)
{
    CoordinatorRegistry registry;
    registry.OfGroup(Group)->GiveUpResurrect(Mage);
    registry.For(Group, RagefireChasm, LeaderOutside, 0)->GiveUpResurrect(Mage);

    registry.Drop(Group);

    EXPECT_FALSE(registry.OfGroup(Group)->IsResurrectGivenUp(Mage));
    EXPECT_FALSE(registry.For(Group, RagefireChasm, LeaderOutside, 0)->IsResurrectGivenUp(Mage));
}

// The group events are of the leader's group: the bots away from the leader
// are no part of them.
TEST(CoordinatorRegistry, OnlyTheGroupCoordinatorMakesGroupEvents)
{
    CoordinatorRegistry registry;
    registry.OfGroup(Group)->SetResting(Mage, RestReason::Eat);
    auto const away = registry.For(Group, RagefireChasm, LeaderOutside, 0);
    away->SetResting(Mage, RestReason::Drink);

    auto const events = registry.TakeEvents();

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].first, Group);
    EXPECT_EQ(events[0].second.kind, GroupEventKind::Rest);
    EXPECT_EQ(events[0].second.reason, RestReason::Eat);
    EXPECT_TRUE(away->TakeEvents().empty());
}
