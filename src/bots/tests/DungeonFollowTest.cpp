#include "DungeonFollow.h"

#include <gtest/gtest.h>

using Bots::BotRole;
using Bots::CommandState;
using Bots::DungeonFollowFacts;
using Bots::FollowsTankThroughInstance;
using Bots::GroupMember;
using Bots::HumanTankOf;
using Bots::MapPlace;
using Bots::NoGuid;
using Bots::TravelFacts;

// jvbastel/WOWAI#196: when the player tank goes into an instance or out of
// one, the bots of the group are teleported after them, next to them, with
// no command.
namespace
{
    Bots::Guid const Player = 0x0000000000000001;
    Bots::Guid const BotTank = 0x0000000000000011;
    Bots::Guid const BotHealer = 0x0000000000000012;

    MapPlace const Durotar = {1, 0};
    MapPlace const RagefireChasm = {389, 7};
    MapPlace const OtherRagefireChasm = {389, 8};
    MapPlace const EasternKingdoms = {0, 0};

    TravelFacts Outside(MapPlace place = Durotar) { return {true, true, place, false}; }
    TravelFacts Inside(MapPlace place = RagefireChasm) { return {true, true, place, true}; }

    // A bot outside Ragefire Chasm in a player-tank group, and the tank
    // inside: the bot follows.
    DungeonFollowFacts TankZonedIn()
    {
        DungeonFollowFacts facts;
        facts.scriptedRun = false;
        facts.humanTank = true;
        facts.tank = Inside();
        facts.bot = Outside();
        facts.command = CommandState::Follow;
        facts.msSinceLastTry = Bots::DungeonFollowRetryMs;
        return facts;
    }

    GroupMember Human(Bots::Guid guid) { return {guid, false, BotRole::Ranged}; }
    GroupMember Bot(Bots::Guid guid, BotRole role) { return {guid, true, role}; }
}

TEST(DungeonFollow, ABotFollowsThePlayerTankIntoTheInstance)
{
    EXPECT_TRUE(FollowsTankThroughInstance(TankZonedIn()));
}

TEST(DungeonFollow, ABotFollowsThePlayerTankOutOfTheInstance)
{
    DungeonFollowFacts facts = TankZonedIn();
    facts.tank = Outside();
    facts.bot = Inside();
    EXPECT_TRUE(FollowsTankThroughInstance(facts));
}

TEST(DungeonFollow, ABotInTheSamePlaceAsTheTankStays)
{
    DungeonFollowFacts facts = TankZonedIn();
    facts.bot = Inside();
    EXPECT_FALSE(FollowsTankThroughInstance(facts));
}

TEST(DungeonFollow, ABotInAnotherInstanceOfTheDungeonFollowsTheTankToItsInstance)
{
    DungeonFollowFacts facts = TankZonedIn();
    facts.bot = Inside(OtherRagefireChasm);
    EXPECT_TRUE(FollowsTankThroughInstance(facts));
}

TEST(DungeonFollow, ABotDoesNotFollowATankWhoTravelsBetweenTwoOpenWorldMaps)
{
    // Only an instance entry or exit: a boat to another continent is no
    // dungeon entry.
    DungeonFollowFacts facts = TankZonedIn();
    facts.tank = Outside(EasternKingdoms);
    EXPECT_FALSE(FollowsTankThroughInstance(facts));
}

TEST(DungeonFollow, ABotOfABotTankGroupDoesNotFollow)
{
    DungeonFollowFacts facts = TankZonedIn();
    facts.humanTank = false;
    EXPECT_FALSE(FollowsTankThroughInstance(facts));
}

TEST(DungeonFollow, ABotOfAScriptedRunDoesNotFollow)
{
    // The run teleports its group itself (BotRun).
    DungeonFollowFacts facts = TankZonedIn();
    facts.scriptedRun = true;
    EXPECT_FALSE(FollowsTankThroughInstance(facts));
}

TEST(DungeonFollow, ADeadBotOrAGhostDoesNotFollow)
{
    // Today's death handling: no teleport of a corpse. A ghost walks back
    // (GhostWalk.h).
    DungeonFollowFacts facts = TankZonedIn();
    facts.bot.alive = false;
    EXPECT_FALSE(FollowsTankThroughInstance(facts));
}

TEST(DungeonFollow, ABotDoesNotFollowADeadTankOrItsGhost)
{
    // A tank who releases its spirit in the dungeon goes to the graveyard
    // outside: the bots stay with the corpse.
    DungeonFollowFacts facts = TankZonedIn();
    facts.tank = Outside();
    facts.tank.alive = false;
    facts.bot = Inside();
    EXPECT_FALSE(FollowsTankThroughInstance(facts));
}

TEST(DungeonFollow, ABotWaitsUntilTheTankHasLanded)
{
    // In the loading screen the tank is out of the world.
    DungeonFollowFacts facts = TankZonedIn();
    facts.tank.inWorld = false;
    EXPECT_FALSE(FollowsTankThroughInstance(facts));
}

TEST(DungeonFollow, ABotInATeleportDoesNotStartAnother)
{
    DungeonFollowFacts facts = TankZonedIn();
    facts.bot.inWorld = false;
    EXPECT_FALSE(FollowsTankThroughInstance(facts));
}

TEST(DungeonFollow, ABotToldToStayDoesNotFollow)
{
    // A bot command is an override.
    DungeonFollowFacts facts = TankZonedIn();
    facts.command = CommandState::Stay;
    EXPECT_FALSE(FollowsTankThroughInstance(facts));
}

TEST(DungeonFollow, ABotWithNoCommandStateFollows)
{
    DungeonFollowFacts facts = TankZonedIn();
    facts.command = CommandState::None;
    EXPECT_TRUE(FollowsTankThroughInstance(facts));
}

TEST(DungeonFollow, AFailedTeleportIsTriedAgainOnlyAfterTheRetryTime)
{
    // The game can refuse the teleport (a full instance): no try at each
    // update.
    DungeonFollowFacts facts = TankZonedIn();
    facts.msSinceLastTry = Bots::DungeonFollowRetryMs - 1;
    EXPECT_FALSE(FollowsTankThroughInstance(facts));
    facts.msSinceLastTry = Bots::DungeonFollowRetryMs;
    EXPECT_TRUE(FollowsTankThroughInstance(facts));
}

TEST(DungeonFollow, TheHumanLeaderOfAGroupWithNoBotTankIsTheTankToFollow)
{
    // Members on any map: the tank zoned in, the bots are still outside.
    EXPECT_EQ(HumanTankOf({Human(Player), Bot(BotHealer, BotRole::Healer)}, Player), Player);
}

TEST(DungeonFollow, AGroupWithABotTankHasNoHumanTankToFollow)
{
    EXPECT_EQ(HumanTankOf({Human(Player), Bot(BotTank, BotRole::Tank), Bot(BotHealer, BotRole::Healer)}, Player),
        NoGuid);
}

TEST(DungeonFollow, AGroupWhoseHumanLeaderIsOutOfTheWorldHasNoTankToFollow)
{
    // The game layer leaves out a member that is not in the world.
    EXPECT_EQ(HumanTankOf({Bot(BotHealer, BotRole::Healer)}, Player), NoGuid);
}
