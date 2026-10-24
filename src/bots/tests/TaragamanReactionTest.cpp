#include "BossReactions.h"
#include "ClassModules.h"
#include "MageModule.h"
#include "TaragamanReaction.h"
#include "FakeBot.h"

#include <gtest/gtest.h>

using Bots::BotContext;
using Bots::BotRole;
using Bots::GroupCoordinator;
using Bots::NoGuid;
using Bots::Position;
using Bots::StrategyStack;
using Bots::Tests::FakeBot;

namespace
{
    Bots::Guid const Tank = 0x0000000000000011;
    Bots::Guid const Healer = 0x0000000000000012;
    Bots::Guid const Mage = 0x0000000000000013;
    Bots::Guid const Rogue = 0x0000000000000014;
    Bots::Guid const Taragaman = 0xF130002D00000044;

    std::vector<std::string> Moves(std::string const& move)
    {
        return {move};
    }

    // One bot of the group in the fight with Taragaman, who stands at the
    // origin and attacks the tank. Only the boss reaction is on the stack.
    struct InFight
    {
        FakeBot fake;
        GroupCoordinator group;
        StrategyStack stack;
        BotRole role;

        InFight(Bots::Guid self, BotRole role, Position where) : fake(self), role(role)
        {
            fake.SetGroupMembers({Tank, Healer, Mage, Rogue});
            for (Bots::Guid member : {Tank, Healer, Mage, Rogue})
                fake.SetHealth(member, 600, 600);
            fake.SetHealth(Taragaman, 5000, 5000);
            fake.SetEntry(Taragaman, Bots::Taragaman::Entry);
            fake.SetPosition(Taragaman, {0, 0, 0});
            fake.SetVictim(Taragaman, Tank);
            fake.SetPosition(self, where);
            group.StartBossFight(Taragaman, Bots::Taragaman::Entry);
            stack.Push(Bots::MakeBossReaction(Bots::Taragaman::Entry, role));
        }

        std::string Think()
        {
            BotContext const context = {fake, fake, group, Bots::CommandState::Follow, Tank, NoGuid};
            return stack.Think(context);
        }
    };
}

TEST(TaragamanReaction, ARangedBotInFireNovaRangeMovesOutAwayFromHim)
{
    InFight mage(Mage, BotRole::Ranged, {5, 0, 0});

    EXPECT_EQ(mage.Think(), "stay out of fire nova");
    EXPECT_EQ(mage.fake.Deeds(), Moves("move to 16 0 0"));
}

TEST(TaragamanReaction, TheHealerInFireNovaRangeMovesOut)
{
    InFight healer(Healer, BotRole::Healer, {0, -6, 2});

    EXPECT_EQ(healer.Think(), "stay out of fire nova");
    EXPECT_EQ(healer.fake.Deeds(), Moves("move to 0 -16 2"));
}

TEST(TaragamanReaction, ARangedBotMoreThanTenYardsAwayStays)
{
    InFight mage(Mage, BotRole::Ranged, {0, 13, 0});

    EXPECT_EQ(mage.Think(), "");
    EXPECT_TRUE(mage.fake.Deeds().empty());
}

TEST(TaragamanReaction, ARangedBotWithAWallBehindItMovesOutToTheSide)
{
    InFight mage(Mage, BotRole::Ranged, {5, 0, 0});
    // The wall is 10 yards behind Taragaman's line through the bot: 30
    // degrees off, it is still nearer than 16 yards; 60 degrees off, not.
    mage.fake.AddWallAtX(10);

    EXPECT_EQ(mage.Think(), "stay out of fire nova");
    EXPECT_EQ(mage.fake.Deeds(), Moves("move to 8 13.86 0"));
}

TEST(TaragamanReaction, ARangedBotHasTheReactionOnTopOfItsClassRules)
{
    InFight mage(Mage, BotRole::Ranged, {5, 0, 0});
    mage.stack = Bots::MakeStrategyStack(Bots::BotClass::Mage, BotRole::Ranged);
    mage.stack.Push(Bots::MakeBossReaction(Bots::Taragaman::Entry, BotRole::Ranged));
    mage.fake.SetKnownSpell(Bots::Mage::Frostbolt);
    mage.fake.SetPower(Mage, 400, 400);
    mage.group.SetFocusTarget(Taragaman);

    EXPECT_EQ(mage.Think(), "stay out of fire nova");
}

TEST(TaragamanReaction, TheTankWalksToANearWallWithHimInFront)
{
    InFight tank(Tank, BotRole::Tank, {2, 0, 0});
    tank.fake.AddWallAtX(10);
    // A wall to the side of Taragaman is no wall behind the tank.
    tank.fake.AddWallAtY(-9);

    EXPECT_EQ(tank.Think(), "back to the wall");
    EXPECT_EQ(tank.fake.Deeds(), Moves("move to 8.5 0 0"));
}

TEST(TaragamanReaction, TheTankWithItsBackAtTheWallStays)
{
    InFight tank(Tank, BotRole::Tank, {8.5, 0, 0});
    tank.fake.AddWallAtX(10);

    EXPECT_EQ(tank.Think(), "");
}

TEST(TaragamanReaction, TheTankWithNoWallNearStays)
{
    InFight tank(Tank, BotRole::Tank, {2, 0, 0});
    tank.fake.AddWallAtX(30);

    EXPECT_EQ(tank.Think(), "");
}

TEST(TaragamanReaction, TheTankThatHasNoThreatDoesNotWalkToTheWall)
{
    InFight tank(Tank, BotRole::Tank, {2, 0, 0});
    tank.fake.AddWallAtX(10);
    tank.fake.SetVictim(Taragaman, Healer);

    EXPECT_EQ(tank.Think(), "");
}

TEST(TaragamanReaction, AMeleeBotStaysOnHim)
{
    InFight rogue(Rogue, BotRole::Melee, {2, 0, 0});

    EXPECT_EQ(rogue.Think(), "");
    EXPECT_TRUE(rogue.stack.Contains("taragaman the hungerer"));
}

TEST(TaragamanReaction, NoReactionAfterHeDies)
{
    InFight mage(Mage, BotRole::Ranged, {5, 0, 0});
    mage.fake.SetHealth(Taragaman, 0, 5000);

    EXPECT_EQ(mage.Think(), "");
}
