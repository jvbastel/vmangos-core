#include "GroupTank.h"

#include <gtest/gtest.h>

using Bots::BotRole;
using Bots::GroupMember;
using Bots::NoGuid;
using Bots::NextGroupTank;
using Bots::PickGroupTank;

// Review of spec #131, story 10: the group's tank is the bot with the tank
// role. With none, it is the player who leads the group: a human leader
// with a group of bots and no bot tank tanks the fights. The game keeps no
// record of a player's role (no main tank flag in a party of 1.12).
namespace
{
    Bots::Guid const Player = 0x0000000000000001;
    Bots::Guid const BotTank = 0x0000000000000011;
    Bots::Guid const BotHealer = 0x0000000000000012;
    Bots::Guid const BotMage = 0x0000000000000013;

    GroupMember Human(Bots::Guid guid) { return {guid, false, BotRole::Ranged}; }
    GroupMember Bot(Bots::Guid guid, BotRole role) { return {guid, true, role}; }
}

TEST(GroupTank, TheBotWithTheTankRoleIsTheTank)
{
    EXPECT_EQ(PickGroupTank({Human(Player), Bot(BotTank, BotRole::Tank), Bot(BotHealer, BotRole::Healer)}, Player),
        BotTank);
}

TEST(GroupTank, WithNoBotTankTheHumanLeaderIsTheTank)
{
    EXPECT_EQ(PickGroupTank({Human(Player), Bot(BotHealer, BotRole::Healer), Bot(BotMage, BotRole::Ranged)}, Player),
        Player);
}

TEST(GroupTank, ABotLeaderWithNoBotTankGivesNoTank)
{
    EXPECT_EQ(PickGroupTank({Bot(BotHealer, BotRole::Healer), Bot(BotMage, BotRole::Ranged)}, BotHealer), NoGuid);
}

TEST(GroupTank, ALeaderOutOfTheMembersGivesNoTank)
{
    // The leader is on another map: the bots do not see it.
    EXPECT_EQ(PickGroupTank({Bot(BotHealer, BotRole::Healer), Bot(BotMage, BotRole::Ranged)}, Player), NoGuid);
}


// Merge review of spec #131: a human tank who leaves the map, or gives up
// the lead, is no tank any more. A bot tank stays (it sets itself).
TEST(GroupTank, AHumanTankWhoLeavesIsNoTankAnyMore)
{
    EXPECT_EQ(NextGroupTank({Bot(BotHealer, BotRole::Healer)}, Player, Player, false), NoGuid);
    EXPECT_EQ(NextGroupTank({Human(Player), Bot(BotHealer, BotRole::Healer)}, BotHealer, Player, false), NoGuid);
}

TEST(GroupTank, ABotTankOutOfTheMembersStaysTheTank)
{
    EXPECT_EQ(NextGroupTank({Bot(BotHealer, BotRole::Healer)}, BotHealer, BotTank, true), BotTank);
}

TEST(GroupTank, APickReplacesTheCurrentTank)
{
    EXPECT_EQ(NextGroupTank({Human(Player), Bot(BotHealer, BotRole::Healer)}, Player, NoGuid, false), Player);
}

// jvbastel/WOWAI#189: with no bot tank, the human leader is the group's
// tank whatever its class: the members carry no class any more. A bot tank
// still wins.
TEST(GroupTank, AHumanLeaderOfAnyClassWithNoBotTankIsTheTank)
{
    EXPECT_EQ(PickGroupTank({Human(Player), Bot(BotHealer, BotRole::Healer)}, Player), Player);
    EXPECT_EQ(PickGroupTank({Human(Player), Bot(BotHealer, BotRole::Healer), Bot(BotTank, BotRole::Tank)}, Player),
        BotTank);
}

TEST(GroupTank, OnlyAHumanTankMakesAPlayerTankGroup)
{
    std::vector<GroupMember> const members = {Human(Player), Bot(BotTank, BotRole::Tank)};
    EXPECT_TRUE(Bots::IsHumanTank(members, Player));
    EXPECT_FALSE(Bots::IsHumanTank(members, BotTank));
    EXPECT_FALSE(Bots::IsHumanTank(members, NoGuid));
}
