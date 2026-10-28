#include "BotGeometry.h"
#include "ClassModules.h"
#include "DamageRules.h"
#include "DeathAndRest.h"
#include "FakeBot.h"
#include "GroupCoordinator.h"
#include "HealerSight.h"
#include "PlayerTank.h"
#include "PriestModule.h"

#include <gtest/gtest.h>

#include <iomanip>
#include <sstream>

using Bots::BotContext;
using Bots::GroupCoordinator;
using Bots::LifeState;
using Bots::NoGuid;
using Bots::Position;
using Bots::StrategyStack;
using Bots::Tests::FakeBot;
namespace Priest = Bots::Priest;

// jvbastel/WOWAI#195 (spec #188, stories 37, 39-41): in a player-tank group
// the healer keeps the tank in sight and in heal reach in a fight; when the
// tank dies mid-fight, the focus stays and the damage bots fight on; after
// the fight the healer resurrects the tank before it rests.
namespace
{
    Bots::Guid const Human = 0x1;
    Bots::Guid const Healer = 0x12;
    Bots::Guid const Mage = 0x13;
    Bots::Guid const Rogue = 0x14;
    Bots::Guid const BotTank = 0x11;
    Bots::Guid const Kobold = 0xF130000302000022;
    Bots::Guid const Wolf = 0xF130000302000033;

    // A bot of a player-tank group: the human tank, a Priest healer, a
    // mage, and a rogue, all at full health, the healer's buffs on all.
    struct PlayerTankBot
    {
        FakeBot fake;
        GroupCoordinator group;
        StrategyStack stack;
        std::int64_t nowMs = 0;

        PlayerTankBot(Bots::Guid self, Bots::BotClass botClass, Bots::BotRole role) :
            fake(self), stack(Bots::MakeStrategyStack(botClass, role))
        {
            std::vector<Bots::Guid> others;
            for (Bots::Guid member : {Human, Healer, Mage, Rogue})
            {
                fake.SetHealth(member, 600, 600);
                fake.SetAura(member, Priest::PowerWordFortitude);
                if (member != self)
                    others.push_back(member);
            }
            fake.SetGroupMembers(others);
            fake.SetGroupLeader(Human);
            fake.SetCanResurrect(Healer);
            fake.SetAura(Healer, Priest::InnerFire);
            for (Bots::Guid member : {Healer, Mage})
            {
                fake.SetMana(member, 400, 400);
                fake.SetPowerKind(member, Bots::PowerKind::Mana);
            }
            fake.SetPower(self, 400, 400);
            fake.SetHealth(Kobold, 300, 300);
            fake.SetHealth(Wolf, 300, 300);
            group.SetTank(Human, true);
        }

        std::string Think()
        {
            BotContext context{fake, fake, group, Bots::CommandState::Follow, Human};
            context.nowMs = nowMs;
            return stack.Think(context);
        }

        void Die(Bots::Guid member)
        {
            fake.SetHealth(member, 0, 600);
            fake.SetLifeState(member, LifeState::Dead);
        }

        // The group fights the kobold: the tank holds it, and the tank has
        // held threat on it long enough for the damage bots.
        void InFight()
        {
            for (Bots::Guid member : {Human, Healer, Mage, Rogue})
                fake.SetInCombat(member);
            fake.SetInCombat(Kobold);
            fake.SetVictim(Kobold, Human);
            fake.SetThreatRatio(Human, Kobold, Bots::UnrivalledThreat);
            fake.SetTargetOf(Human, Kobold);
            Bots::WatchPlayerTank(fake, group, 0);
            nowMs = Bots::PlayerTankHoldMs;
            Bots::WatchPlayerTank(fake, group, nowMs);
        }

        // The watches of the bot AI before a think.
        void Watch()
        {
            Bots::WatchGroup(fake, group);
            Bots::WatchPlayerTank(fake, group, nowMs);
        }

        Bots::Guid DamageTarget()
        {
            BotContext context{fake, fake, group, Bots::CommandState::Follow, Human};
            context.nowMs = nowMs;
            return Bots::PickDamageTarget(context);
        }
    };

    struct PlayerTankHealer : PlayerTankBot
    {
        PlayerTankHealer() : PlayerTankBot(Healer, Bots::BotClass::Priest, Bots::BotRole::Healer)
        {
            for (Bots::SpellId spell : {Priest::LesserHeal, Priest::Heal, Priest::Renew, Priest::PowerWordShield,
                     Priest::PowerWordFortitude, Priest::InnerFire, Priest::Fade})
                fake.SetKnownSpell(spell);
        }
    };

    // The move of a deed "keep move to x y z", or false.
    bool MoveOf(std::string const& deed, Position& to)
    {
        std::istringstream in(deed);
        std::string keep, move, word;
        return in >> keep >> move >> word >> to.x >> to.y >> to.z && keep == "keep" && move == "move";
    }
}

// Story 37: the tank stepped behind a corner (a line-of-sight pull). The
// healer walks to a spot that sees the tank and is in heal reach.
TEST(PlayerTankHealerSight, TheHealerMovesToASpotThatSeesTheTankInReach)
{
    PlayerTankHealer bot;
    bot.InFight();
    bot.fake.SetPosition(Healer, {-20, 10, 0});
    bot.fake.SetPosition(Human, {5, 10, 0});
    bot.fake.SetPosition(Kobold, {7, 10, 0});
    bot.fake.AddSightWall({-5, 2, 0}, {-5, 60, 0});
    ASSERT_FALSE(bot.fake.CanSeeFrom(bot.fake.GetPosition(Healer), Human));

    EXPECT_EQ(bot.Think(), "keep the tank in sight");
    ASSERT_EQ(bot.fake.Deeds().size(), 1u);
    Position spot{};
    ASSERT_TRUE(MoveOf(bot.fake.Deeds().front(), spot)) << bot.fake.Deeds().front();
    EXPECT_TRUE(bot.fake.CanSeeFrom(spot, Human));
    EXPECT_LE(Bots::Distance3d(spot, bot.fake.GetPosition(Human)), Priest::HealReachYards);
}

TEST(PlayerTankHealerSight, TheHealerWalksBackIntoHealReachOfTheTank)
{
    PlayerTankHealer bot;
    bot.InFight();
    bot.fake.SetPosition(Healer, {-50, 0, 0});
    bot.fake.SetPosition(Human, {0, 0, 0});
    bot.fake.SetPosition(Kobold, {2, 0, 0});

    EXPECT_EQ(bot.Think(), "keep the tank in sight");
    Position spot{};
    ASSERT_TRUE(MoveOf(bot.fake.Deeds().front(), spot)) << bot.fake.Deeds().front();
    EXPECT_LE(Bots::Distance3d(spot, bot.fake.GetPosition(Human)), Priest::HealReachYards);
}

TEST(PlayerTankHealerSight, AHealerThatSeesTheTankInReachStays)
{
    PlayerTankHealer bot;
    bot.InFight();
    bot.fake.SetPosition(Healer, {-20, 0, 0});
    bot.fake.SetPosition(Human, {0, 0, 0});
    bot.fake.SetPosition(Kobold, {2, 0, 0});

    EXPECT_NE(bot.Think(), "keep the tank in sight");
}

// A step would stop the heal.
TEST(PlayerTankHealerSight, TheHealerDoesNotMoveWhileItCasts)
{
    PlayerTankHealer bot;
    bot.InFight();
    bot.fake.SetPosition(Healer, {-50, 0, 0});
    bot.fake.SetPosition(Human, {0, 0, 0});
    bot.fake.SetCurrentCast(Healer, Priest::Renew);

    EXPECT_NE(bot.Think(), "keep the tank in sight");
}

// Story 36: a bot-tank group keeps today's healer.
TEST(PlayerTankHealerSight, ABotTankGroupHasNoSightRule)
{
    PlayerTankHealer bot;
    bot.InFight();
    bot.group.SetTank(BotTank);
    bot.fake.SetHealth(BotTank, 600, 600);
    bot.fake.SetInCombat(BotTank);
    bot.fake.SetPosition(Healer, {-50, 0, 0});
    bot.fake.SetPosition(BotTank, {0, 0, 0});

    EXPECT_NE(bot.Think(), "keep the tank in sight");
}

TEST(PlayerTankHealerSight, OutOfAFightTheHealerDoesNotWalkToTheTank)
{
    PlayerTankHealer bot;
    bot.fake.SetPosition(Healer, {-50, 0, 0});
    bot.fake.SetPosition(Human, {0, 0, 0});

    EXPECT_NE(bot.Think(), "keep the tank in sight");
}

TEST(PlayerTankHealerSight, TheHealerDoesNotWalkToADeadTank)
{
    PlayerTankHealer bot;
    bot.InFight();
    bot.Die(Human);
    bot.fake.SetPosition(Healer, {-50, 0, 0});
    bot.fake.SetPosition(Human, {0, 0, 0});

    EXPECT_NE(bot.Think(), "keep the tank in sight");
}

// Story 39: the tank died mid-fight. The kobold turns on the healer, and a
// wolf hits the mage: the damage bots keep on the focus.
TEST(PlayerTankDeath, TheDamageBotsKeepAttackingTheFocusWhenTheTankDies)
{
    for (Bots::Guid self : {Mage, Rogue})
    {
        PlayerTankBot bot(self, self == Mage ? Bots::BotClass::Mage : Bots::BotClass::Rogue,
            self == Mage ? Bots::BotRole::Ranged : Bots::BotRole::Melee);
        bot.InFight();
        ASSERT_EQ(bot.DamageTarget(), Kobold);

        bot.Die(Human);
        bot.fake.SetVictim(Kobold, Healer);
        bot.fake.SetInCombat(Wolf);
        bot.fake.SetVictim(Wolf, Mage);
        bot.Watch();

        EXPECT_EQ(bot.group.GetFocusTarget(), Kobold);
        EXPECT_EQ(bot.DamageTarget(), Kobold) << "bot " << self;
    }
}

// The tank died before it held threat for PlayerTankHoldMs: no tank holds
// threat any more, so the damage bots no longer wait.
TEST(PlayerTankDeath, TheDamageBotsDoNotWaitForADeadTank)
{
    PlayerTankBot bot(Mage, Bots::BotClass::Mage, Bots::BotRole::Ranged);
    for (Bots::Guid member : {Human, Healer, Mage, Rogue})
        bot.fake.SetInCombat(member);
    bot.fake.SetInCombat(Kobold);
    bot.fake.SetVictim(Kobold, Human);
    bot.fake.SetThreatRatio(Human, Kobold, Bots::UnrivalledThreat);
    bot.fake.SetTargetOf(Human, Kobold);
    bot.Watch();
    ASSERT_EQ(bot.DamageTarget(), NoGuid);

    bot.Die(Human);
    bot.fake.SetVictim(Kobold, Healer);
    bot.nowMs = 500;
    bot.Watch();
    EXPECT_EQ(bot.DamageTarget(), Kobold);
}

// The dead tank still has a target: the focus stays as it is.
TEST(PlayerTankDeath, TheFocusStaysWhenTheTankDies)
{
    PlayerTankBot bot(Mage, Bots::BotClass::Mage, Bots::BotRole::Ranged);
    bot.InFight();
    bot.Die(Human);
    bot.fake.SetInCombat(Wolf);
    bot.fake.SetVictim(Wolf, Mage);
    bot.fake.SetTargetOf(Human, Wolf);
    bot.Watch();

    EXPECT_EQ(bot.group.GetFocusTarget(), Kobold);
}

// The focus died after the tank: the bots defend themselves against the
// loose enemies, as today.
TEST(PlayerTankDeath, AfterTheFocusDiesTheBotsTakeTheLooseEnemies)
{
    PlayerTankBot bot(Mage, Bots::BotClass::Mage, Bots::BotRole::Ranged);
    bot.InFight();
    bot.Die(Human);
    bot.fake.SetInCombat(Wolf);
    bot.fake.SetVictim(Wolf, Mage);
    bot.fake.SetHealth(Kobold, 0, 300);
    bot.Watch();

    EXPECT_EQ(bot.DamageTarget(), Wolf);
}

// Story 40: after the fight, the healer, low on mana, resurrects the tank
// first, and then rests. (A healer low on health heals itself first: the
// heals of the living come before the resurrect.)
TEST(PlayerTankDeath, AfterTheFightTheHealerResurrectsTheTankBeforeItRests)
{
    PlayerTankHealer bot;
    bot.fake.SetKnownSpell(Priest::Resurrection);
    bot.fake.SetCastsOnDead(Priest::Resurrection);
    bot.Die(Human);
    bot.fake.SetMana(Healer, 150, 400);
    bot.fake.SetPower(Healer, 150, 400);
    bot.Watch();

    EXPECT_EQ(bot.Think(), "resurrect a member");
    std::ostringstream cast;
    cast << "cast " << Priest::Resurrection << " on 0x" << std::hex << std::setw(16) << std::setfill('0') << Human;
    EXPECT_EQ(bot.fake.Deeds(), (std::vector<std::string>{cast.str()}));

    // The tank lives again: now the healer drinks.
    bot.fake.ClearDeeds();
    bot.fake.SetLifeState(Human, LifeState::Alive);
    bot.fake.SetHealth(Human, 600, 600);
    bot.Watch();
    EXPECT_EQ(bot.Think(), "drink");
}

// Story 41: a full wipe keeps today's death handling: the bots wait for the
// leader (the dead tank) to release.
TEST(PlayerTankDeath, InAFullWipeTheBotsWaitForTheTankToRelease)
{
    PlayerTankHealer bot;
    for (Bots::Guid member : {Human, Healer, Mage, Rogue})
        bot.Die(member);
    bot.Watch();

    EXPECT_TRUE(bot.group.IsWiped());
    EXPECT_EQ(bot.Think(), "wait dead");

    bot.fake.SetLifeState(Human, LifeState::Ghost);
    EXPECT_EQ(bot.Think(), "release with the leader");
}

// jvbastel/WOWAI#195, merge review: story 40 for a healer that already sits
// to drink when the fight ends. A sitting bot casts nothing (the game
// layer), so it must stand up first: ResurrectsTankBeforeRest tells the
// game layer to stand it up, and the rest rules give way to the resurrect.
namespace
{
    struct DrinkingHealerWithDeadTank : PlayerTankHealer
    {
        DrinkingHealerWithDeadTank()
        {
            fake.SetKnownSpell(Priest::Resurrection);
            fake.SetCastsOnDead(Priest::Resurrection);
            fake.SetRankAmounts(Priest::Resurrection, 0, 100);
            Die(Human);
            fake.SetMana(Healer, 150, 400);
            fake.SetPower(Healer, 150, 400);
            fake.SetDrinking();
            Watch();
        }
    };
}

TEST(PlayerTankDeath, ASittingHealerStandsUpToResurrectTheDeadTank)
{
    DrinkingHealerWithDeadTank bot;
    EXPECT_TRUE(Bots::ResurrectsTankBeforeRest(bot.fake, bot.group));
    EXPECT_EQ(bot.Think(), "resurrect a member");
}

TEST(PlayerTankDeath, AHealerThatCannotCastYetDoesNotSitDownWhileTheTankIsDead)
{
    // The resurrect waits a tick (the cooldown of the last heal): the
    // healer neither keeps resting nor starts to drink.
    DrinkingHealerWithDeadTank bot;
    bot.fake.SetCannotCast(Priest::Resurrection);
    EXPECT_NE(bot.Think(), "keep resting");
    bot.fake.SetDrinking(false);
    EXPECT_NE(bot.Think(), "drink");
}

TEST(PlayerTankDeath, AHealerWithoutTheManaForTheResurrectDrinksFirst)
{
    DrinkingHealerWithDeadTank bot;
    bot.fake.SetMana(Healer, 50, 400);
    bot.fake.SetPower(Healer, 50, 400);
    EXPECT_FALSE(Bots::ResurrectsTankBeforeRest(bot.fake, bot.group));
    // The game layer refuses the cast (no mana, and the bot sits).
    bot.fake.SetCannotCast(Priest::Resurrection);
    EXPECT_EQ(bot.Think(), "keep resting");
}

TEST(PlayerTankDeath, InABotTankGroupTheSittingHealerKeepsResting)
{
    DrinkingHealerWithDeadTank bot;
    bot.group.SetTank(Human, false);
    EXPECT_FALSE(Bots::ResurrectsTankBeforeRest(bot.fake, bot.group));
}

TEST(PlayerTankDeath, TheHealerResurrectsTheTankBeforeOtherDeadMembers)
{
    PlayerTankHealer bot;
    bot.fake.SetKnownSpell(Priest::Resurrection);
    bot.fake.SetCastsOnDead(Priest::Resurrection);
    bot.fake.SetGroupMembers({Mage, Human, Rogue});
    bot.Die(Mage);
    bot.Die(Human);
    bot.Watch();

    EXPECT_EQ(bot.Think(), "resurrect a member");
    std::ostringstream cast;
    cast << "cast " << Priest::Resurrection << " on 0x" << std::hex << std::setw(16) << std::setfill('0') << Human;
    EXPECT_EQ(bot.fake.Deeds(), (std::vector<std::string>{cast.str()}));
}
