#include "ClassModules.h"
#include "DamageRules.h"
#include "FakeBot.h"
#include "GroupCoordinator.h"
#include "PlayerTank.h"
#include "MageModule.h"
#include "RogueModule.h"

#include <gtest/gtest.h>

using Bots::BotContext;
using Bots::GroupCoordinator;
using Bots::NoGuid;
using Bots::Tests::FakeBot;

// jvbastel/WOWAI#189: the player tank. A human leads a group of bots with no
// bot tank and types no bot command: the focus is the tank's target, once it
// is an enemy in combat with the group, and the damage bots attack it after
// the tank has been top of its threat list for PlayerTankHoldMs.
namespace
{
    Bots::Guid const Human = 0x1;
    Bots::Guid const Rogue = 0x13;
    Bots::Guid const Healer = 0x12;
    Bots::Guid const Kobold = 0xF130000302000022;
    Bots::Guid const Wolf = 0xF130000302000033;

    struct PlayerTankGroup
    {
        FakeBot fake{Rogue};
        GroupCoordinator group;

        PlayerTankGroup()
        {
            fake.SetGroupMembers({Human, Healer});
            fake.SetHealth(Human, 600, 600);
            fake.SetHealth(Healer, 300, 300);
            fake.SetHealth(Rogue, 400, 400);
            fake.SetHealth(Kobold, 200, 200);
            fake.SetHealth(Wolf, 200, 200);
            group.SetTank(Human, true);
        }

        // Enemy `enemy` fights the group: it attacks the tank, which has
        // threat on it.
        void Fights(Bots::Guid enemy, float tankRatio = Bots::UnrivalledThreat)
        {
            fake.SetVictim(enemy, Human);
            fake.SetThreatRatio(Human, enemy, tankRatio);
        }

        void Watch(std::int64_t nowMs) { Bots::WatchPlayerTank(fake, group, nowMs); }

        Bots::Guid Target(std::int64_t nowMs)
        {
            BotContext context{fake, fake, group, Bots::CommandState::Follow, Human, NoGuid};
            context.nowMs = nowMs;
            return Bots::PickDamageTarget(context);
        }
    };
}

TEST(PlayerTankFocus, TheFocusFollowsTheTanksTargetInCombat)
{
    PlayerTankGroup bots;
    bots.Fights(Kobold);
    bots.Fights(Wolf);

    bots.fake.SetTargetOf(Human, Kobold);
    bots.Watch(0);
    EXPECT_EQ(bots.group.GetFocusTarget(), Kobold);

    bots.fake.SetTargetOf(Human, Wolf);
    bots.Watch(100);
    EXPECT_EQ(bots.group.GetFocusTarget(), Wolf);
}

TEST(PlayerTankFocus, AFriendlyOrEmptyTargetKeepsTheLastFocus)
{
    PlayerTankGroup bots;
    bots.Fights(Kobold);
    bots.fake.SetTargetOf(Human, Kobold);
    bots.Watch(0);

    bots.fake.SetTargetOf(Human, Healer);
    bots.Watch(100);
    EXPECT_EQ(bots.group.GetFocusTarget(), Kobold);

    bots.fake.SetTargetOf(Human, NoGuid);
    bots.Watch(200);
    EXPECT_EQ(bots.group.GetFocusTarget(), Kobold);
}

TEST(PlayerTankFocus, AnEnemyNotInCombatWithTheGroupIsNotTheFocus)
{
    PlayerTankGroup bots;
    // The wolf stands in its pack: no fight, no threat of any member.
    bots.fake.SetTargetOf(Human, Wolf);
    bots.Watch(0);
    EXPECT_EQ(bots.group.GetFocusTarget(), NoGuid);

    // The wolf fights another player, out of the group.
    bots.fake.SetVictim(Wolf, 0x99);
    bots.Watch(100);
    EXPECT_EQ(bots.group.GetFocusTarget(), NoGuid);
}

TEST(PlayerTankFocus, AnEnemyThatHitsAMemberIsInCombatWithTheGroup)
{
    PlayerTankGroup bots;
    // The kobold hits the healer; the tank has no threat on it yet.
    bots.fake.SetVictim(Kobold, Healer);
    bots.fake.SetTargetOf(Human, Kobold);
    bots.Watch(0);
    EXPECT_EQ(bots.group.GetFocusTarget(), Kobold);
}

TEST(PlayerTankFocus, ABotTankGroupKeepsItsOwnFocus)
{
    PlayerTankGroup bots;
    bots.group.SetTank(0x11);
    bots.Fights(Kobold);
    bots.fake.SetTargetOf(Human, Kobold);
    bots.Watch(0);
    EXPECT_EQ(bots.group.GetFocusTarget(), NoGuid);
}

TEST(PlayerTankThreat, ADamageBotWaitsTwoSecondsOfTheTankOnTop)
{
    PlayerTankGroup bots;
    bots.Fights(Kobold, 1.4f);
    bots.fake.SetTargetOf(Human, Kobold);

    bots.Watch(0);
    EXPECT_EQ(bots.Target(0), NoGuid);
    bots.Watch(1900);
    EXPECT_EQ(bots.Target(1900), NoGuid);
    bots.Watch(2000);
    EXPECT_EQ(bots.Target(2000), Kobold);
}

TEST(PlayerTankThreat, TheWaitStartsWhenTheTankGetsOnTop)
{
    PlayerTankGroup bots;
    // The healer is above the tank on the kobold at first.
    bots.Fights(Kobold, 0.8f);
    bots.fake.SetTargetOf(Human, Kobold);
    bots.Watch(0);
    bots.Watch(1000);
    bots.fake.SetThreatRatio(Human, Kobold, 1.2f);
    bots.Watch(1500);
    bots.Watch(3000);
    EXPECT_EQ(bots.Target(3000), NoGuid);
    bots.Watch(3500);
    EXPECT_EQ(bots.Target(3500), Kobold);

    // The healer gets on top: the bots wait again.
    bots.fake.SetThreatRatio(Human, Kobold, 0.9f);
    bots.Watch(3600);
    EXPECT_EQ(bots.Target(3600), NoGuid);
}

TEST(PlayerTankThreat, ANewFocusStartsTheWaitAgain)
{
    PlayerTankGroup bots;
    bots.Fights(Kobold);
    bots.Fights(Wolf);
    bots.fake.SetTargetOf(Human, Kobold);
    bots.Watch(0);
    bots.Watch(2000);
    EXPECT_EQ(bots.Target(2000), Kobold);

    bots.fake.SetTargetOf(Human, Wolf);
    bots.Watch(2100);
    EXPECT_EQ(bots.Target(2100), NoGuid);
    bots.Watch(4100);
    EXPECT_EQ(bots.Target(4100), Wolf);
}

// Merge review of jvbastel/WOWAI#189: a bot that does not see the tank or
// the focus (its view gives no threat) notes nothing. It does not start the
// clock again for the bots that see them.
TEST(PlayerTankThreat, ABotThatDoesNotSeeTheFocusKeepsTheClock)
{
    PlayerTankGroup bots;
    FakeBot away{Healer};
    away.SetGroupMembers({Human, Rogue});
    away.SetTargetOf(Human, Kobold);
    bots.Fights(Kobold);
    bots.fake.SetTargetOf(Human, Kobold);

    bots.Watch(0);
    Bots::WatchPlayerTank(away, bots.group, 1000);
    bots.Watch(1500);
    Bots::WatchPlayerTank(away, bots.group, 1900);
    bots.Watch(2000);
    EXPECT_EQ(bots.Target(2000), Kobold);
}

// jvbastel/WOWAI#191: the skull and cross marks set the focus in a
// player-tank group. Skull in combat with the group, else cross in combat
// with the group, else the tank's target (jvbastel/WOWAI#189).
namespace
{
    Bots::Guid const Murloc = 0xF130000302000044;

    struct MarkedGroup : PlayerTankGroup
    {
        MarkedGroup()
        {
            fake.SetHealth(Murloc, 200, 200);
        }

        void Marks(Bots::Guid skull, Bots::Guid cross)
        {
            Bots::BotView::GroupMarks marks;
            marks.skull = skull;
            marks.cross = cross;
            fake.SetGroupMarks(marks);
        }
    };
}

TEST(PlayerTankMarks, ASkullInCombatBeatsTheCrossAndTheTanksTarget)
{
    MarkedGroup bots;
    bots.Fights(Kobold);
    bots.Fights(Wolf);
    bots.Fights(Murloc);
    bots.Marks(Wolf, Murloc);
    bots.fake.SetTargetOf(Human, Kobold);
    bots.Watch(0);
    EXPECT_EQ(bots.group.GetFocusTarget(), Wolf);
}

TEST(PlayerTankMarks, WhenTheSkullDiesTheCrossInCombatIsTheFocus)
{
    MarkedGroup bots;
    bots.Fights(Kobold);
    bots.Fights(Wolf);
    bots.Fights(Murloc);
    bots.Marks(Wolf, Murloc);
    bots.fake.SetTargetOf(Human, Kobold);
    bots.Watch(0);

    bots.fake.SetHealth(Wolf, 0, 200);
    bots.Watch(100);
    EXPECT_EQ(bots.group.GetFocusTarget(), Murloc);
}

TEST(PlayerTankMarks, WithNoLiveMarksInCombatTheTanksTargetIsTheFocus)
{
    MarkedGroup bots;
    bots.Fights(Kobold);
    bots.Fights(Wolf);
    bots.Fights(Murloc);
    bots.Marks(Wolf, Murloc);
    bots.fake.SetTargetOf(Human, Kobold);
    bots.Watch(0);

    bots.fake.SetHealth(Wolf, 0, 200);
    bots.fake.SetHealth(Murloc, 0, 200);
    bots.Watch(100);
    EXPECT_EQ(bots.group.GetFocusTarget(), Kobold);

    // The game clears no mark of a dead enemy at once; with no marks at
    // all, the tank's target is the focus too.
    bots.Marks(NoGuid, NoGuid);
    bots.fake.SetTargetOf(Human, Wolf);
    bots.fake.SetHealth(Wolf, 200, 200);
    bots.Watch(200);
    EXPECT_EQ(bots.group.GetFocusTarget(), Wolf);
}

namespace
{
    bool AnyDeedOn(FakeBot const& fake, Bots::Guid unit)
    {
        char hex[24];
        std::snprintf(hex, sizeof(hex), "0x%016llx", static_cast<unsigned long long>(unit));
        for (std::string const& deed : fake.Deeds())
            if (deed.find(hex) != std::string::npos)
                return true;
        return false;
    }

    // A damage bot of the marked group thinks once at `nowMs`, after the
    // player-tank watch.
    std::string ThinkAs(MarkedGroup& bots, Bots::BotClass botClass, Bots::BotRole role, std::int64_t nowMs)
    {
        Bots::StrategyStack stack = Bots::MakeStrategyStack(botClass, role);
        bots.Watch(nowMs);
        BotContext context{bots.fake, bots.fake, bots.group, Bots::CommandState::Follow, Bots::Guid(0x1), NoGuid};
        context.nowMs = nowMs;
        return stack.Think(context);
    }
}

TEST(PlayerTankMarks, ASkullOutOfCombatDoesNotChangeTheFocusAndNoBotGoesToIt)
{
    MarkedGroup bots;
    for (Bots::SpellId spell : {Bots::Rogue::SinisterStrike, Bots::Rogue::Eviscerate, Bots::Rogue::SliceAndDice,
             Bots::Mage::Fireball, Bots::Mage::Frostbolt})
        bots.fake.SetKnownSpell(spell);
    bots.fake.SetPower(Rogue, 100, 100);
    bots.fake.SetFollowTarget(Human);
    bots.Fights(Kobold);
    // The wolf stands in its pack with the skull on it: no fight.
    bots.Marks(Wolf, NoGuid);
    bots.fake.SetTargetOf(Human, Kobold);

    bots.Watch(0);
    EXPECT_EQ(bots.group.GetFocusTarget(), Kobold);

    for (Bots::BotRole role : {Bots::BotRole::Melee, Bots::BotRole::Ranged})
    {
        Bots::BotClass const botClass = role == Bots::BotRole::Melee ? Bots::BotClass::Rogue : Bots::BotClass::Mage;
        bots.fake.ClearDeeds();
        ThinkAs(bots, botClass, role, 1000);
        ThinkAs(bots, botClass, role, 2500);
        EXPECT_EQ(bots.group.GetFocusTarget(), Kobold);
        EXPECT_FALSE(AnyDeedOn(bots.fake, Wolf));
        // The bot fights the focus once the tank holds threat on it.
        EXPECT_TRUE(AnyDeedOn(bots.fake, Kobold));
    }
}

TEST(PlayerTankMarks, WhileTheSkullLivesADamageBotLeavesTheCross)
{
    MarkedGroup bots;
    bots.Fights(Wolf);
    bots.Fights(Murloc);
    bots.Marks(Wolf, Murloc);
    bots.fake.SetTargetOf(Human, Murloc);
    bots.Watch(0);
    bots.Watch(2000);
    EXPECT_EQ(bots.Target(2000), Wolf);

    // The tank turns to the cross: the skull still lives, it stays the focus.
    bots.Watch(2500);
    EXPECT_EQ(bots.group.GetFocusTarget(), Wolf);
    EXPECT_EQ(bots.Target(2500), Wolf);
}

TEST(PlayerTankMarks, ACrossThatAttacksAMeleeBotAtTheTankIsALooseEnemy)
{
    MarkedGroup bots;
    bots.Fights(Wolf);
    bots.Marks(Wolf, Murloc);
    bots.fake.SetTargetOf(Human, Wolf);
    // The cross hits the rogue, next to the tank; the tank does not yet
    // hold the skull, so the rogue has no focus to hit.
    bots.fake.SetVictim(Murloc, Rogue);
    bots.fake.SetDistance(Human, 2.0f);
    bots.Watch(0);
    BotContext context{bots.fake, bots.fake, bots.group, Bots::CommandState::Follow, Human, NoGuid};
    context.nowMs = 0;
    EXPECT_EQ(Bots::PickMeleeDamageTarget(context), Murloc);
}
