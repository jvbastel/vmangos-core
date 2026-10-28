#include "ClassModules.h"
#include "MageModule.h"
#include "RogueModule.h"
#include "FakeBot.h"
#include "PlayerTank.h"

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
            Bots::Tests::HoldThreat(group, Tank);
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

TEST(Rogue, BuildsToFiveComboPointsOnAHealthyTarget)
{
    RogueBot rogue;
    rogue.FightsKoboldFromBehind();
    rogue.fake.SetAura(Self, Rogue::SliceAndDice);
    rogue.fake.SetHealth(Kobold, 60, 200);
    rogue.fake.SetComboPoints(4);

    rogue.Think();

    EXPECT_EQ(rogue.fake.Deeds(), (std::vector<std::string>{CastOn(Rogue::SinisterStrike, Kobold)}));
}

TEST(Rogue, KeepsComboPointsOnADyingTargetWithNone)
{
    RogueBot rogue;
    rogue.FightsKoboldFromBehind();
    rogue.fake.SetAura(Self, Rogue::SliceAndDice);
    rogue.fake.SetHealth(Kobold, 30, 200);

    rogue.Think();

    EXPECT_EQ(rogue.fake.Deeds(), (std::vector<std::string>{CastOn(Rogue::SinisterStrike, Kobold)}));
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

// Final review of spec #142, finding 2: Frost Nova freezes the enemy that
// the Rogue already hits. The running swing would break the freeze, so the
// Rogue stops it, unless the tank holds the enemy or the player chose it.
namespace
{
    Bots::Guid const Healer = 0x0000000000000012;

    // The tank holds the kobold. The Rogue hits the wolf, which hits the
    // healer, and the wolf is frozen.
    struct RogueOnAFrozenWolf : RogueBot
    {
        RogueOnAFrozenWolf()
        {
            fake.SetGroupMembers({Tank, Healer, Self});
            group.SetTank(Tank);
            for (Bots::Guid unit : {Tank, Healer})
                fake.SetHealth(unit, 400, 400);
            TankHoldsKobold();
            fake.SetVictim(Wolf, Healer);
            fake.SetVictim(Self, Wolf);
            fake.ChaseBehind(Wolf);
            fake.SetAura(Wolf, Bots::Mage::FrostNova);
            fake.ClearDeeds();
        }
    };
}

TEST(Rogue, StopsItsSwingOnAFrozenEnemy)
{
    RogueOnAFrozenWolf rogue;

    rogue.Think();

    EXPECT_EQ(rogue.fake.Deeds(), (std::vector<std::string>{"stop the attack"}));
}

TEST(Rogue, GoesOnToTheTanksTargetAfterTheStop)
{
    RogueOnAFrozenWolf rogue;
    rogue.Think();
    rogue.fake.ClearDeeds();

    rogue.Think();

    EXPECT_EQ(rogue.fake.Deeds(), (std::vector<std::string>{"attack 0xf130000302000022", "chase behind 0xf130000302000022"}));
}

TEST(Rogue, KeepsHittingAFrozenEnemyThatTheTankHolds)
{
    RogueOnAFrozenWolf rogue;
    rogue.fake.SetVictim(Kobold, NoGuid);
    rogue.group.SetFocusTarget(Wolf);
    rogue.fake.SetVictim(Wolf, Tank);

    rogue.Think();

    for (std::string const& deed : rogue.fake.Deeds())
        EXPECT_NE(deed, "stop the attack");
}

TEST(Rogue, KeepsHittingAFrozenAttackTargetOfThePlayer)
{
    RogueOnAFrozenWolf rogue;
    rogue.attackTarget = Wolf;

    rogue.Think();

    for (std::string const& deed : rogue.fake.Deeds())
        EXPECT_NE(deed, "stop the attack");
}

TEST(Rogue, ADeadRogueStopsNothing)
{
    RogueOnAFrozenWolf rogue;
    rogue.fake.SetHealth(Self, 0, 400);

    rogue.Think();

    for (std::string const& deed : rogue.fake.Deeds())
        EXPECT_NE(deed, "stop the attack");
}

// jvbastel/WOWAI#194: the threat throttle in a player-tank group. At the
// threat stop (90% of the melee takeover, 110% of the tank's threat), the
// Rogue uses Feint when it knows it, else it stops its swing.
namespace
{
    // The player tanks: it held the kobold for the threat lead, and the
    // Rogue swings at it with `ratio` of the tank's threat.
    void PlayerTankHoldsKobold(RogueBot& rogue, float ratio)
    {
        rogue.fake.SetGroupMembers({Player});
        rogue.fake.SetDistance(Player, 3.0f);
        rogue.group.SetTank(Player, true);
        rogue.fake.SetVictim(Kobold, Player);
        rogue.fake.SetTargetOf(Player, Kobold);
        rogue.fake.SetThreatRatio(Player, Kobold, 1.5f);
        Bots::WatchPlayerTank(rogue.fake, rogue.group, 0);
        Bots::WatchPlayerTank(rogue.fake, rogue.group, Bots::PlayerTankHoldMs);
        rogue.fake.SetThreatRatio(Self, Kobold, ratio);
        rogue.fake.SetVictim(Self, Kobold);
        rogue.fake.ChaseBehind(Kobold);
        rogue.fake.ClearDeeds();
    }
}

TEST(RoguePlayerTank, FeintsAtTheThreatStop)
{
    RogueBot rogue;
    rogue.fake.SetKnownSpell(Rogue::Feint);
    PlayerTankHoldsKobold(rogue, 1.0f);

    BotContext context{rogue.fake, rogue.fake, rogue.group, Bots::CommandState::Follow, Player, NoGuid};
    context.nowMs = Bots::PlayerTankHoldMs + 100;
    EXPECT_EQ(rogue.stack.Think(context), "feint at the threat stop");
    EXPECT_EQ(rogue.fake.Deeds(), (std::vector<std::string>{CastOn(Rogue::Feint, Kobold)}));
}

TEST(RoguePlayerTank, StopsTheSwingAtTheThreatStopWithoutFeint)
{
    RogueBot rogue;
    PlayerTankHoldsKobold(rogue, 1.0f);

    BotContext context{rogue.fake, rogue.fake, rogue.group, Bots::CommandState::Follow, Player, NoGuid};
    context.nowMs = Bots::PlayerTankHoldMs + 100;
    rogue.stack.Think(context);
    EXPECT_EQ(rogue.fake.Deeds(), (std::vector<std::string>{"stop the attack"}));
}

TEST(RoguePlayerTank, SwingsOnUnderTheThreatStop)
{
    RogueBot rogue;
    rogue.fake.SetKnownSpell(Rogue::Feint);
    PlayerTankHoldsKobold(rogue, 0.98f);

    BotContext context{rogue.fake, rogue.fake, rogue.group, Bots::CommandState::Follow, Player, NoGuid};
    context.nowMs = Bots::PlayerTankHoldMs + 100;
    EXPECT_EQ(rogue.stack.Think(context), "sinister strike");
}
