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

// Merge review of jvbastel/WOWAI#189: a bot tank that is not in `members`
// (on another map, or zoning) stays the tank. The human leader does not take
// over and make a player-tank group of a bot-tank group.
TEST(GroupTank, ABotTankOutOfTheMembersStaysTheTankOverAHumanLeader)
{
    EXPECT_EQ(NextGroupTank({Human(Player), Bot(BotHealer, BotRole::Healer)}, Player, BotTank, true), BotTank);
}

// jvbastel/WOWAI#197: the role choice of the add-on. A human who picked tank
// is the tank, also with a bot tank in the group. With no choice, the rule
// of #189 above applies. Never two tanks: such a group sends its bot tank
// away, and takes no new one.
namespace
{
    GroupMember HumanWith(Bots::Guid guid, Bots::RoleChoice choice) { return {guid, false, BotRole::Ranged, choice}; }
}

TEST(GroupTank, AHumanWithTheTankRoleChoiceIsTheTankOverABotTank)
{
    EXPECT_EQ(PickGroupTank({Bot(BotTank, BotRole::Tank), HumanWith(Player, Bots::RoleChoice::Tank)}, Player), Player);
}

TEST(GroupTank, AHumanWithTheTankRoleChoiceIsTheTankWhenNotTheLeader)
{
    Bots::Guid const Other = 0x0000000000000002;
    EXPECT_EQ(PickGroupTank({Human(Other), HumanWith(Player, Bots::RoleChoice::Tank), Bot(BotHealer, BotRole::Healer)},
                  Other),
        Player);
}

TEST(GroupTank, WithNoRoleChoiceTheTracerRuleApplies)
{
    EXPECT_EQ(PickGroupTank({HumanWith(Player, Bots::RoleChoice::None), Bot(BotTank, BotRole::Tank)}, Player), BotTank);
    EXPECT_EQ(PickGroupTank({HumanWith(Player, Bots::RoleChoice::None), Bot(BotHealer, BotRole::Healer)}, Player),
        Player);
}

TEST(GroupTank, ANonTankRoleChoiceChangesNothing)
{
    EXPECT_EQ(PickGroupTank({HumanWith(Player, Bots::RoleChoice::Healer), Bot(BotTank, BotRole::Tank)}, Player), BotTank);
    EXPECT_EQ(PickGroupTank({HumanWith(Player, Bots::RoleChoice::Melee), Bot(BotHealer, BotRole::Healer)}, Player),
        Player);
}

TEST(GroupTank, TheTankRoleChoiceTakesOverFromABotTankThatIsAway)
{
    EXPECT_EQ(NextGroupTank({HumanWith(Player, Bots::RoleChoice::Tank), Bot(BotHealer, BotRole::Healer)}, Player, BotTank,
                  true),
        Player);
    EXPECT_TRUE(Bots::IsHumanTank({HumanWith(Player, Bots::RoleChoice::Tank)}, Player));
}

TEST(GroupTank, AGroupWithAHumanTankRoleChoiceTakesNoBotTank)
{
    EXPECT_TRUE(Bots::RefusesBotTank({HumanWith(Player, Bots::RoleChoice::Tank), Bot(BotHealer, BotRole::Healer)}));
    EXPECT_FALSE(Bots::RefusesBotTank({HumanWith(Player, Bots::RoleChoice::Healer), Bot(BotHealer, BotRole::Healer)}));
    EXPECT_FALSE(Bots::RefusesBotTank({Human(Player), Bot(BotHealer, BotRole::Healer)}));
}

TEST(GroupTank, ABotTankLeavesAGroupWhoseHumanPickedTank)
{
    std::vector<GroupMember> const members = {HumanWith(Player, Bots::RoleChoice::Tank), Bot(BotTank, BotRole::Tank),
        Bot(BotHealer, BotRole::Healer)};

    EXPECT_TRUE(Bots::MustLeaveAsSecondTank(members, BotTank));
    EXPECT_FALSE(Bots::MustLeaveAsSecondTank(members, BotHealer));
}

TEST(GroupTank, ABotTankStaysWithNoHumanTankRoleChoice)
{
    EXPECT_FALSE(Bots::MustLeaveAsSecondTank({Human(Player), Bot(BotTank, BotRole::Tank)}, BotTank));
    EXPECT_FALSE(
        Bots::MustLeaveAsSecondTank({HumanWith(Player, Bots::RoleChoice::Ranged), Bot(BotTank, BotRole::Tank)}, BotTank));
}
