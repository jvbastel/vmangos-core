#include "ClassModules.h"
#include "RogueModule.h"
#include "FakeBot.h"

#include <gtest/gtest.h>

using Bots::BotContext;
using Bots::GroupCoordinator;
using Bots::NoGuid;
using Bots::StrategyStack;
using Bots::Tests::FakeBot;
namespace Rogue = Bots::Rogue;

namespace
{
    Bots::Guid const Tank = 0x0000000000000011;
    Bots::Guid const Self = 0x0000000000000013;
    Bots::Guid const Player = 0x0000000000000001;
    Bots::Guid const Kobold = 0xF130000302000022;
    Bots::Guid const Wolf = 0xF130000302000033;

    std::string CastOn(Bots::SpellId spell, Bots::Guid target)
    {
        char text[64];
        std::snprintf(text, sizeof(text), "cast %u on 0x%016llx", spell, static_cast<unsigned long long>(target));
        return text;
    }

    // A level 16 Rogue that follows the player, in a group with a tank.
    struct RogueBot
    {
        FakeBot fake{Self};
        GroupCoordinator group;
        StrategyStack stack = Bots::MakeStrategyStack(Bots::BotClass::Rogue, Bots::BotRole::Melee);
        Bots::Guid attackTarget = NoGuid;

        RogueBot()
        {
            for (Bots::SpellId spell : {Rogue::SinisterStrike, Rogue::Eviscerate, Rogue::SliceAndDice})
                fake.SetKnownSpell(spell);
            fake.SetHealth(Self, 400, 400);
            fake.SetPower(Self, 100, 100);
            fake.SetHealth(Player, 400, 400);
            fake.SetFollowTarget(Player);
            fake.SetHealth(Kobold, 200, 200);
            fake.SetHealth(Wolf, 200, 200);
        }

        std::string Think()
        {
            BotContext const context{fake, fake, group, Bots::CommandState::Follow, Player, attackTarget};
            return stack.Think(context);
        }

        // The tank pulled the kobold, and the kobold attacks the tank.
        void TankHoldsKobold()
        {
            group.StartPull(Tank, Kobold);
            fake.SetVictim(Kobold, Tank);
            group.HoldPull(Tank);
        }

        // The Rogue hits the kobold from behind.
        void FightsKoboldFromBehind()
        {
            TankHoldsKobold();
            fake.SetVictim(Self, Kobold);
            fake.ChaseBehind(Kobold);
            fake.ClearDeeds();
        }
    };
}

TEST(Rogue, WaitsWhileTheTankPulls)
{
    RogueBot rogue;
    rogue.group.StartPull(Tank, Kobold);

    rogue.Think();

    EXPECT_TRUE(rogue.fake.Deeds().empty());
}

TEST(Rogue, AttacksTheFocusTargetFromBehindOnceTheTankHoldsThreat)
{
    RogueBot rogue;
    rogue.TankHoldsKobold();

    rogue.Think();

    EXPECT_EQ(rogue.fake.Deeds(), (std::vector<std::string>{"attack 0xf130000302000022", "chase behind 0xf130000302000022"}));
}

TEST(Rogue, GetsBehindTheTargetWhenItStandsInFront)
{
    RogueBot rogue;
    rogue.TankHoldsKobold();
    rogue.fake.SetVictim(Self, Kobold);
    rogue.fake.Chase(Kobold);
    rogue.fake.ClearDeeds();

    rogue.Think();

    EXPECT_EQ(rogue.fake.Deeds(), (std::vector<std::string>{"chase behind 0xf130000302000022"}));
}

TEST(Rogue, FacesATargetThatAttacksIt)
{
    RogueBot rogue;
    rogue.FightsKoboldFromBehind();
    rogue.fake.SetVictim(Kobold, Self);

    rogue.Think();

    EXPECT_EQ(rogue.fake.Deeds(), (std::vector<std::string>{"chase 0xf130000302000022"}));
}

TEST(Rogue, SinisterStrikeBuildsComboPoints)
{
    RogueBot rogue;
    rogue.FightsKoboldFromBehind();

    rogue.Think();

    EXPECT_EQ(rogue.fake.Deeds(), (std::vector<std::string>{CastOn(Rogue::SinisterStrike, Kobold)}));
}

TEST(Rogue, SliceAndDiceWithTwoComboPointsOnTheTarget)
{
    RogueBot rogue;
    rogue.FightsKoboldFromBehind();
    rogue.fake.SetComboPoints(2);

    rogue.Think();

    EXPECT_EQ(rogue.fake.Deeds(), (std::vector<std::string>{CastOn(Rogue::SliceAndDice, Kobold)}));
}

TEST(Rogue, EviscerateWithFiveComboPoints)
{
    RogueBot rogue;
    rogue.FightsKoboldFromBehind();
    rogue.fake.SetAura(Self, Rogue::SliceAndDice);
    rogue.fake.SetComboPoints(5);

    rogue.Think();

    EXPECT_EQ(rogue.fake.Deeds(), (std::vector<std::string>{CastOn(Rogue::Eviscerate, Kobold)}));
}

TEST(Rogue, KeepsComboPointsBelowFiveForSinisterStrike)
{
    RogueBot rogue;
    rogue.FightsKoboldFromBehind();
    rogue.fake.SetAura(Self, Rogue::SliceAndDice);
    rogue.fake.SetComboPoints(3);

    rogue.Think();

    EXPECT_EQ(rogue.fake.Deeds(), (std::vector<std::string>{CastOn(Rogue::SinisterStrike, Kobold)}));
}

TEST(Rogue, EviscerateWithFewerComboPointsOnADyingTarget)
{
    RogueBot rogue;
    rogue.FightsKoboldFromBehind();
    rogue.fake.SetAura(Self, Rogue::SliceAndDice);
    rogue.fake.SetHealth(Kobold, 30, 200);
    rogue.fake.SetComboPoints(2);

    rogue.Think();

    EXPECT_EQ(rogue.fake.Deeds(), (std::vector<std::string>{CastOn(Rogue::Eviscerate, Kobold)}));
}

TEST(Rogue, SwitchesToTheAttackTargetOfThePlayer)
{
    RogueBot rogue;
    rogue.FightsKoboldFromBehind();
    rogue.attackTarget = Wolf;

    rogue.Think();

    EXPECT_EQ(rogue.fake.Deeds(), (std::vector<std::string>{"attack 0xf130000302000033", "chase behind 0xf130000302000033"}));
}

TEST(Rogue, FollowsThePlayerOutOfCombat)
{
    RogueBot rogue;
    rogue.fake.SetFollowTarget(NoGuid);

    rogue.Think();

    EXPECT_EQ(rogue.fake.Deeds(), (std::vector<std::string>{"follow 0x0000000000000001"}));
}
