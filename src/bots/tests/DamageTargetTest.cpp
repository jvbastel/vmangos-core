#include "DamageRules.h"
#include "FakeBot.h"
#include "GroupRules.h"
#include "MageModule.h"

#include <gtest/gtest.h>

using Bots::BotContext;
using Bots::GroupCoordinator;
using Bots::NoGuid;
using Bots::PickDamageTarget;
using Bots::Tests::FakeBot;

// The targeting rules of the damage bots: they attack the target of the tank
// once the tank holds threat, or the target of the player after `attack`.
namespace
{
    Bots::Guid const Tank = 0x11;
    Bots::Guid const Rogue = 0x13;
    Bots::Guid const Kobold = 0xF130000302000022;
    Bots::Guid const Wolf = 0xF130000302000033;

    struct DamageBot
    {
        FakeBot fake{Rogue};
        GroupCoordinator group;

        DamageBot()
        {
            fake.SetHealth(Rogue, 400, 400);
            fake.SetHealth(Kobold, 200, 200);
            fake.SetHealth(Wolf, 200, 200);
        }

        Bots::Guid Target(Bots::Guid attackTarget = NoGuid, std::int64_t nowMs = 0)
        {
            BotContext context{fake, fake, group, Bots::CommandState::Follow, 0x1, attackTarget};
            context.nowMs = nowMs;
            return PickDamageTarget(context);
        }

        // The tank pulls the kobold, and at 0 ms it attacks the tank in
        // melee contact: the bot sees it.
        void HoldThePull()
        {
            group.StartPull(Tank, Kobold);
            group.HoldPull(Tank, 0);
            fake.SetVictim(Kobold, Tank);
            fake.SetDistanceBetween(Kobold, Tank, 3.0f);
            Target(NoGuid, 0);
        }
    };
}

TEST(DamageTarget, NoTargetWithoutAFight)
{
    DamageBot bot;

    EXPECT_EQ(bot.Target(), NoGuid);
}

TEST(DamageTarget, WaitsWhileTheTankPulls)
{
    DamageBot bot;
    bot.group.StartPull(Tank, Kobold);

    EXPECT_EQ(bot.Target(), NoGuid);
}

// jvbastel/WOWAI#127: the tank holds threat HoldContactMs after the
// melee contact, not at once on HoldPull.
TEST(DamageTarget, TheFocusTargetOnceTheTankHoldsThreat)
{
    DamageBot bot;
    bot.HoldThePull();

    EXPECT_EQ(bot.Target(NoGuid, Bots::HoldContactMs), Kobold);
}

TEST(DamageTarget, TheFocusTargetWhenTheTankFightsBackOutsideAPull)
{
    DamageBot bot;
    bot.group.SetFocusTarget(Wolf);

    EXPECT_EQ(bot.Target(), Wolf);
}

// Outside a pull, the focus that still hits another member is not the
// tank's yet: the damage of the others would add to their threat on it, and
// the tank's taunt only evens the threat (WOWAI#73 review).
TEST(DamageTarget, WaitsWhileTheFocusOutsideAPullHitsAnotherMember)
{
    DamageBot bot;
    Bots::Guid const Healer = 0x12;
    bot.fake.SetHealth(Tank, 600, 600);
    bot.group.SetTank(Tank);
    bot.group.SetFocusTarget(Wolf);
    bot.fake.SetVictim(Wolf, Healer);

    EXPECT_EQ(bot.Target(), NoGuid);

    bot.fake.SetVictim(Wolf, Tank);

    EXPECT_EQ(bot.Target(), Wolf);
}

// The taunt does not come (the tank cannot cast it): the damage bots attack
// the focus that hits the healer, or nobody ever does (WOWAI#93 review, the
// row 32 case).
TEST(DamageTarget, TheFocusOnAnotherMemberWhenNoTauntComes)
{
    DamageBot bot;
    Bots::Guid const Healer = 0x12;
    bot.fake.SetHealth(Tank, 600, 600);
    bot.group.SetTank(Tank);
    bot.group.SetFocusTarget(Wolf);
    bot.fake.SetVictim(Wolf, Healer);
    bot.group.GiveUpTaunt(Wolf);

    EXPECT_EQ(bot.Target(), Wolf);
}

// A new focus of the tank beyond the chase limit is not chased: the damage
// bots stay with the tank (WOWAI#87).
TEST(DamageTarget, NoChaseOfANewFocusBeyondTheChaseLimit)
{
    DamageBot bot;
    bot.fake.SetHealth(Tank, 600, 600);
    bot.group.SetTank(Tank);
    bot.group.SetFocusTarget(Wolf);
    bot.fake.SetVictim(Wolf, Tank);
    bot.fake.SetDistanceBetween(Wolf, Tank, Bots::ChaseLimitYards + 10.0f);

    EXPECT_EQ(bot.Target(), NoGuid);

    bot.fake.SetDistanceBetween(Wolf, Tank, Bots::ChaseLimitYards - 5.0f);

    EXPECT_EQ(bot.Target(), Wolf);
}

TEST(DamageTarget, ANewFocusWaitsForItsTauntAgain)
{
    DamageBot bot;
    Bots::Guid const Healer = 0x12;
    bot.fake.SetHealth(Tank, 600, 600);
    bot.group.SetTank(Tank);
    bot.group.SetFocusTarget(Wolf);
    bot.group.GiveUpTaunt(Wolf);
    bot.group.SetFocusTarget(Kobold);
    bot.fake.SetVictim(Kobold, Healer);

    EXPECT_EQ(bot.Target(), NoGuid);
}

TEST(DamageTarget, TheFocusOnAnotherMemberWhenTheTankIsDead)
{
    DamageBot bot;
    bot.fake.SetHealth(Tank, 0, 600);
    bot.group.SetTank(Tank);
    bot.group.SetFocusTarget(Wolf);
    bot.fake.SetVictim(Wolf, 0x12);

    EXPECT_EQ(bot.Target(), Wolf);
}

TEST(DamageTarget, TheAttackTargetOfThePlayerComesFirst)
{
    DamageBot bot;
    bot.group.StartPull(Tank, Kobold);
    bot.group.HoldPull(Tank);

    EXPECT_EQ(bot.Target(Wolf), Wolf);
}

TEST(DamageTarget, TheAttackTargetDoesNotWaitForThreat)
{
    DamageBot bot;
    bot.group.StartPull(Tank, Kobold);

    EXPECT_EQ(bot.Target(Wolf), Wolf);
}

TEST(DamageTarget, BackToTheFocusTargetWhenTheAttackTargetDies)
{
    DamageBot bot;
    bot.HoldThePull();
    bot.fake.SetHealth(Wolf, 0, 200);

    EXPECT_EQ(bot.Target(Wolf, Bots::HoldContactMs), Kobold);
}

TEST(DamageTarget, FightsBackAnEnemyThatAttacksTheBot)
{
    DamageBot bot;
    bot.group.StartPull(Tank, Kobold);
    bot.fake.SetVictim(Wolf, Rogue);

    EXPECT_EQ(bot.Target(), Wolf);
}

// A damage bot that stays or follows does not see the tank's fight start: it
// joins when an enemy hits a group member (WOWAI#93).
TEST(DamageTarget, AttacksAnEnemyThatHitsAGroupMember)
{
    DamageBot bot;
    Bots::Guid const Healer = 0x12;
    bot.fake.SetHealth(Tank, 600, 600);
    bot.fake.SetHealth(Healer, 300, 300);
    bot.fake.SetGroupMembers({Tank, Healer});
    bot.group.SetTank(Tank);
    bot.fake.SetVictim(Wolf, Healer);

    EXPECT_EQ(bot.Target(), Wolf);
}

// The tank died in its fight: its focus is gone, and its pack beats the
// healer (WOWAI#93, Ragefire Chasm pack 9).
TEST(DamageTarget, AttacksAnEnemyOnAGroupMemberWhenTheTankIsDead)
{
    DamageBot bot;
    Bots::Guid const Healer = 0x12;
    bot.fake.SetHealth(Tank, 0, 600);
    bot.fake.SetHealth(Healer, 300, 300);
    bot.fake.SetGroupMembers({Tank, Healer});
    bot.group.SetTank(Tank);
    bot.fake.SetDistance(Wolf, 60.0f);
    bot.fake.SetVictim(Wolf, Healer);

    EXPECT_EQ(bot.Target(), Wolf);
}

TEST(DamageTarget, AttacksAnEnemyOnAGroupMemberWhileTheTankPulls)
{
    DamageBot bot;
    Bots::Guid const Healer = 0x12;
    bot.fake.SetHealth(Tank, 600, 600);
    bot.fake.SetHealth(Healer, 300, 300);
    bot.fake.SetGroupMembers({Tank, Healer});
    bot.group.StartPull(Tank, Kobold);
    bot.fake.SetVictim(Wolf, Healer);

    EXPECT_EQ(bot.Target(), Wolf);
}

// A fleeing enemy keeps its victim: far from the tank and from the member,
// it is out of the chase limit.
TEST(DamageTarget, LeavesAnEnemyOnAGroupMemberOutsideTheChaseLimit)
{
    DamageBot bot;
    Bots::Guid const Healer = 0x12;
    bot.fake.SetHealth(Tank, 600, 600);
    bot.fake.SetHealth(Healer, 300, 300);
    bot.fake.SetGroupMembers({Tank, Healer});
    bot.group.SetTank(Tank);
    bot.fake.SetDistanceBetween(Wolf, Tank, Bots::ChaseEnemyYards + 5.0f);
    bot.fake.SetDistanceBetween(Wolf, Healer, Bots::ChaseEnemyYards + 5.0f);
    bot.fake.SetVictim(Wolf, Healer);

    EXPECT_EQ(bot.Target(), NoGuid);
}

// The bot waits for the taunt on the focus, but not on another enemy that
// hits a member.
TEST(DamageTarget, AttacksAnotherEnemyOnAMemberWhileItWaitsForTheTaunt)
{
    DamageBot bot;
    Bots::Guid const Healer = 0x12;
    bot.fake.SetHealth(Tank, 600, 600);
    bot.fake.SetHealth(Healer, 300, 300);
    bot.fake.SetGroupMembers({Tank, Healer});
    bot.group.SetTank(Tank);
    bot.group.SetFocusTarget(Wolf);
    bot.fake.SetVictim(Wolf, Healer);

    EXPECT_EQ(bot.Target(), NoGuid);

    bot.fake.SetVictim(Kobold, Healer);

    EXPECT_EQ(bot.Target(), Kobold);
}

// A loose enemy on a member comes before the focus that the tank holds: no
// bot attacked the second Earthborer on the healer while all hit the focus,
// a lost mob (WOWAI#93, Ragefire Chasm pack 1).
TEST(DamageTarget, ALooseEnemyOnAMemberComesBeforeTheFocus)
{
    DamageBot bot;
    Bots::Guid const Healer = 0x12;
    bot.fake.SetHealth(Tank, 600, 600);
    bot.fake.SetHealth(Healer, 300, 300);
    bot.fake.SetGroupMembers({Tank, Healer});
    bot.group.SetTank(Tank);
    bot.group.StartPull(Tank, Kobold);
    bot.group.HoldPull(Tank);
    bot.fake.SetVictim(Kobold, Tank);
    bot.fake.SetVictim(Wolf, Healer);

    EXPECT_EQ(bot.Target(), Wolf);
}

// Two loose enemies: the bot takes the one that no other member attacks, so
// that no enemy stays lost.
TEST(DamageTarget, TakesTheLooseEnemyThatNoOtherMemberAttacks)
{
    DamageBot bot;
    Bots::Guid const Healer = 0x12;
    Bots::Guid const Mage = 0x14;
    Bots::Guid const Trogg = 0xF130000302000044;
    bot.fake.SetHealth(Tank, 600, 600);
    bot.fake.SetHealth(Healer, 300, 300);
    bot.fake.SetHealth(Mage, 300, 300);
    bot.fake.SetHealth(Trogg, 200, 200);
    bot.fake.SetGroupMembers({Tank, Healer, Mage});
    bot.group.SetTank(Tank);
    bot.fake.SetVictim(Wolf, Healer);
    bot.fake.SetVictim(Trogg, Healer);
    bot.fake.SetVictim(Mage, Wolf);

    EXPECT_EQ(bot.Target(), Trogg);
}

// The bot keeps the loose enemy that only it attacks.
TEST(DamageTarget, KeepsTheLooseEnemyThatOnlyItAttacks)
{
    DamageBot bot;
    Bots::Guid const Healer = 0x12;
    Bots::Guid const Trogg = 0xF130000302000044;
    bot.fake.SetHealth(Tank, 600, 600);
    bot.fake.SetHealth(Healer, 300, 300);
    bot.fake.SetHealth(Trogg, 200, 200);
    bot.fake.SetGroupMembers({Tank, Healer});
    bot.group.SetTank(Tank);
    bot.fake.SetVictim(Wolf, Healer);
    bot.fake.SetVictim(Trogg, Healer);
    bot.fake.SetVictim(Rogue, Trogg);

    EXPECT_EQ(bot.Target(), Trogg);
}

// The attacks of the player hold no enemy in a scripted run (the test driver
// is no bot): the bot does not leave an enemy to it.
TEST(DamageTarget, ALooseEnemyThatThePlayerAttacksIsStillLoose)
{
    DamageBot bot;
    Bots::Guid const Healer = 0x12;
    Bots::Guid const Player = 0x1;
    Bots::Guid const Mage = 0x14;
    Bots::Guid const Trogg = 0xF130000302000044;
    bot.fake.SetGroupMembers({Tank, Healer, Player, Mage});
    bot.fake.SetHealth(Mage, 300, 300);
    bot.fake.SetHealth(Tank, 600, 600);
    bot.fake.SetHealth(Healer, 300, 300);
    bot.fake.SetHealth(Player, 300, 300);
    bot.fake.SetHealth(Trogg, 200, 200);
    bot.group.SetTank(Tank);
    bot.fake.SetVictim(Wolf, Healer);
    bot.fake.SetVictim(Trogg, Healer);
    bot.fake.SetVictim(Player, Trogg);
    bot.fake.SetVictim(Mage, Wolf);

    EXPECT_EQ(bot.Target(), Trogg);
}

// Two bots on one loose enemy, and another loose enemy that nobody attacks:
// only one of them moves on, so that they do not swap back and forth. The
// bot with the lower guid keeps the enemy.
TEST(DamageTarget, OfTwoBotsOnOneLooseEnemyTheLowerGuidKeepsIt)
{
    Bots::Guid const Healer = 0x12;
    Bots::Guid const Mage = 0x14;
    Bots::Guid const Trogg = 0xF130000302000044;
    // The same fight, seen by the rogue (0x13) and by the mage (0x14).
    DamageBot rogue;
    DamageBot mage;
    mage.fake = FakeBot{Mage};
    for (DamageBot* bot : {&rogue, &mage})
    {
        bot->fake.SetGroupMembers({Tank, Healer, bot == &rogue ? Mage : Rogue});
        for (Bots::Guid unit : {Tank, Healer, Mage, Rogue, Wolf, Trogg})
            bot->fake.SetHealth(unit, 300, 300);
        bot->group.SetTank(Tank);
        bot->fake.SetVictim(Wolf, Healer);
        bot->fake.SetVictim(Trogg, Healer);
        bot->fake.SetVictim(Rogue, Trogg);
        bot->fake.SetVictim(Mage, Trogg);
    }

    EXPECT_EQ(rogue.Target(), Trogg);
    EXPECT_EQ(mage.Target(), Wolf);
}

// jvbastel/WOWAI#127: after a ranged pull, the focus comes to the tank. The
// damage bots wait until the tank has it in melee for a while, at its fight
// spot (GroupCoordinator::TankHoldsThreat).
TEST(DamageTarget, WaitsWhileTheRangedPulledFocusRunsToTheTank)
{
    DamageBot bot;
    bot.group.StartPull(Tank, Kobold);
    bot.group.HoldPull(Tank, 1000);
    bot.fake.SetVictim(Kobold, Tank);
    bot.fake.SetMoving(Kobold);
    bot.fake.SetDistanceBetween(Kobold, Tank, 20.0f);

    EXPECT_EQ(bot.Target(NoGuid, 2000), NoGuid);
}

TEST(DamageTarget, TheFocusInMeleeOfTheTankAfterTheContactDelay)
{
    DamageBot bot;
    bot.group.StartPull(Tank, Kobold);
    bot.group.HoldPull(Tank, 1000);
    bot.fake.SetVictim(Kobold, Tank);
    bot.fake.SetDistanceBetween(Kobold, Tank, 20.0f);
    EXPECT_EQ(bot.Target(NoGuid, 2000), NoGuid);

    bot.fake.SetDistanceBetween(Kobold, Tank, 4.0f);
    EXPECT_EQ(bot.Target(NoGuid, 3000), NoGuid);
    EXPECT_EQ(bot.Target(NoGuid, 3000 + Bots::HoldContactMs - 1), NoGuid);
    EXPECT_EQ(bot.Target(NoGuid, 3000 + Bots::HoldContactMs), Kobold);
}

TEST(DamageTarget, WaitsWhileTheTankWalksBackToItsFightSpot)
{
    DamageBot bot;
    bot.group.StartPull(Tank, Kobold);
    bot.group.SetFightSpot(Tank, {10, 0, 0});
    bot.group.HoldPull(Tank, 1000);
    bot.fake.SetVictim(Kobold, Tank);
    bot.fake.SetDistanceBetween(Kobold, Tank, 3.0f);
    EXPECT_EQ(bot.Target(NoGuid, 1000), NoGuid);
    EXPECT_EQ(bot.Target(NoGuid, 1000 + Bots::HoldContactMs), NoGuid);

    // Back at the fight spot ("back at the fight spot", WarriorModule.cpp).
    bot.group.ClearFightSpot(Tank);
    EXPECT_EQ(bot.Target(NoGuid, 1000 + Bots::HoldContactMs), Kobold);
}

TEST(DamageTarget, ACasterThatStaysAtRangeAfterTheTimeLimit)
{
    DamageBot bot;
    bot.group.StartPull(Tank, Kobold);
    bot.group.SetFightSpot(Tank, {10, 0, 0});
    bot.group.HoldPull(Tank, 1000);
    bot.group.ClearFightSpot(Tank);
    bot.fake.SetVictim(Kobold, Tank);
    bot.fake.SetDistanceBetween(Kobold, Tank, 25.0f);

    EXPECT_EQ(bot.Target(NoGuid, 1000 + Bots::HoldLimitMs - 1), NoGuid);
    EXPECT_EQ(bot.Target(NoGuid, 1000 + Bots::HoldLimitMs), Kobold);
}

// While the ranged-pulled focus runs to the tank, a loose enemy on another
// member is still attacked at once.
TEST(DamageTarget, AttacksALooseEnemyWhileTheRangedPulledFocusRunsToTheTank)
{
    DamageBot bot;
    Bots::Guid const Healer = 0x12;
    bot.fake.SetHealth(Tank, 600, 600);
    bot.fake.SetHealth(Healer, 300, 300);
    bot.fake.SetGroupMembers({Tank, Healer});
    bot.group.SetTank(Tank);
    bot.group.StartPull(Tank, Kobold);
    bot.group.HoldPull(Tank, 1000);
    bot.fake.SetVictim(Kobold, Tank);
    bot.fake.SetDistanceBetween(Kobold, Tank, 20.0f);
    bot.fake.SetVictim(Wolf, Healer);

    EXPECT_EQ(bot.Target(NoGuid, 2000), Wolf);
}

// jvbastel/WOWAI#144: ranged damage holds fire on an enemy frozen by Frost
// Nova until the tank is its target or the freeze ends: a hit breaks the
// freeze.
namespace
{
    Bots::Guid const FrozenHealer = 0x12;

    // The tank holds the kobold; the wolf hits the healer, frozen.
    struct FrozenWolfOnTheHealer : DamageBot
    {
        FrozenWolfOnTheHealer()
        {
            fake.SetHealth(Tank, 600, 600);
            fake.SetHealth(FrozenHealer, 300, 300);
            fake.SetGroupMembers({Tank, FrozenHealer});
            group.SetTank(Tank);
            group.StartPull(Tank, Kobold);
            Bots::Tests::HoldThreat(group, Tank);
            fake.SetVictim(Kobold, Tank);
            fake.SetVictim(Wolf, FrozenHealer);
            fake.SetAura(Wolf, Bots::Mage::FrostNova);
        }

        Bots::Guid MeleeTarget(Bots::Guid attackTarget = NoGuid)
        {
            BotContext const context{fake, fake, group, Bots::CommandState::Follow, 0x1, attackTarget};
            return Bots::PickMeleeDamageTarget(context);
        }
    };
}

TEST(DamageTarget, HoldsFireOnAFrozenLooseEnemy)
{
    FrozenWolfOnTheHealer bot;

    EXPECT_EQ(bot.Target(), Kobold);
}

TEST(DamageTarget, AttacksAFrozenEnemyOnceTheTankIsItsTarget)
{
    FrozenWolfOnTheHealer bot;
    bot.group.SetFocusTarget(Wolf);
    bot.fake.SetVictim(Wolf, Tank);

    EXPECT_EQ(bot.Target(), Wolf);
}

TEST(DamageTarget, AttacksTheLooseEnemyOnceTheFreezeEnds)
{
    FrozenWolfOnTheHealer bot;
    bot.fake.SetAura(Wolf, Bots::Mage::FrostNova, false);

    EXPECT_EQ(bot.Target(), Wolf);
}

TEST(DamageTarget, HoldsFireOnAFrozenFocusOnAnotherMember)
{
    FrozenWolfOnTheHealer bot;
    bot.fake.SetVictim(Wolf, Tank);
    bot.fake.SetAura(Wolf, Bots::Mage::FrostNova, false);
    bot.fake.SetVictim(Kobold, FrozenHealer);
    bot.fake.SetAura(Kobold, Bots::Mage::FrostNova);

    EXPECT_EQ(bot.Target(), NoGuid);
}

// jvbastel/WOWAI#142: a melee member never attacks a frozen enemy.
TEST(DamageTarget, MeleeDamageNeverAttacksAFrozenLooseEnemy)
{
    FrozenWolfOnTheHealer bot;

    EXPECT_EQ(bot.MeleeTarget(), Kobold);
}

TEST(DamageTarget, MeleeDamageAttacksTheLooseEnemyOnceTheFreezeEnds)
{
    FrozenWolfOnTheHealer bot;
    bot.fake.SetAura(Wolf, Bots::Mage::FrostNova, false);

    EXPECT_EQ(bot.MeleeTarget(), Wolf);
}

TEST(DamageTarget, MeleeDamageHitsBackAnAttackerThatIsNotFrozen)
{
    FrozenWolfOnTheHealer bot;
    bot.fake.SetAura(Wolf, Bots::Mage::FrostNova, false);
    bot.fake.SetGroupMembers({Tank, FrozenHealer, Rogue});
    bot.fake.SetVictim(Wolf, Rogue);

    EXPECT_EQ(bot.MeleeTarget(), Wolf);
}

TEST(DamageTarget, MeleeDamageDoesNotHitBackAFrozenAttacker)
{
    FrozenWolfOnTheHealer bot;
    bot.fake.SetGroupMembers({Tank, FrozenHealer, Rogue});
    bot.fake.SetVictim(Wolf, Rogue);

    EXPECT_EQ(bot.MeleeTarget(), Kobold);
}

// The player's command wins: the attack target of the player is attacked,
// even when frozen.
TEST(DamageTarget, MeleeDamageAttacksAFrozenAttackTargetOfThePlayer)
{
    FrozenWolfOnTheHealer bot;

    EXPECT_EQ(bot.MeleeTarget(Wolf), Wolf);
}

// Final review of spec #142, finding 5 (lead's decision): melee holds off a
// frozen enemy only until the tank is its target, as ranged does. The
// tank's own hits break that freeze anyway.
TEST(DamageTarget, MeleeDamageAttacksTheTanksFrozenTarget)
{
    FrozenWolfOnTheHealer bot;
    bot.fake.SetVictim(Wolf, Tank);
    bot.fake.SetAura(Wolf, Bots::Mage::FrostNova, false);
    bot.fake.SetAura(Kobold, Bots::Mage::FrostNova);

    EXPECT_EQ(bot.MeleeTarget(), Kobold);
}

TEST(DamageTarget, MeleeDamageAttacksAFrozenEnemyOnceTheTankIsItsTarget)
{
    FrozenWolfOnTheHealer bot;
    bot.group.SetFocusTarget(Wolf);
    bot.fake.SetVictim(Wolf, Tank);
    bot.fake.SetVictim(Kobold, NoGuid);

    EXPECT_EQ(bot.MeleeTarget(), Wolf);
}

TEST(DamageTarget, TheAttackTargetOfThePlayerIsNotHeld)
{
    FrozenWolfOnTheHealer bot;

    EXPECT_EQ(bot.Target(Wolf), Wolf);
}
