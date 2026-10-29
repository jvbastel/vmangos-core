#include "ClassModules.h"
#include "DamageRules.h"
#include "FakeBot.h"
#include "GroupRules.h"
#include "MageModule.h"
#include "RogueModule.h"

#include <gtest/gtest.h>

#include <cstdio>

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
    BotContext const context{bot.fake, bot.fake, bot.group, Bots::CommandState::Follow, 0x1};

    EXPECT_EQ(bot.Target(), NoGuid);
    // The log says why (WhyNoDamageTarget), for ranged and melee bots.
    EXPECT_EQ(Bots::WhyNoDamageTarget(context, false), Bots::NoDamageTarget::WaitForTaunt);
    EXPECT_EQ(Bots::WhyNoDamageTarget(context, true), Bots::NoDamageTarget::WaitForTaunt);

    bot.fake.SetVictim(Wolf, Tank);

    EXPECT_EQ(bot.Target(), Wolf);
    EXPECT_EQ(Bots::WhyNoDamageTarget(context, false), Bots::NoDamageTarget::None);
}

TEST(DamageTarget, WithNoFocusTheReasonIsTheMissingFocus)
{
    DamageBot bot;
    BotContext const context{bot.fake, bot.fake, bot.group, Bots::CommandState::Follow, 0x1};

    EXPECT_EQ(Bots::WhyNoDamageTarget(context, false), Bots::NoDamageTarget::NoFocus);
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

// jvbastel/WOWAI#171: a damage bot does not peel an enemy off a member while
// the tank lives: the tank picks it up, and the bot waits for the focus.
TEST(DamageTarget, LeavesAnEnemyOnAGroupMemberToTheLivingTank)
{
    DamageBot bot;
    Bots::Guid const Healer = 0x12;
    bot.fake.SetHealth(Tank, 600, 600);
    bot.fake.SetHealth(Healer, 300, 300);
    bot.fake.SetGroupMembers({Tank, Healer});
    bot.group.SetTank(Tank);
    bot.fake.SetVictim(Wolf, Healer);

    EXPECT_EQ(bot.Target(), NoGuid);
}

// The tank died in its fight: its focus is gone, and its pack beats the
// healer (WOWAI#93, Ragefire Chasm pack 9).
// Each reason has words for the log line; None (a target) has none.
TEST(DamageTarget, EachNoTargetReasonHasWordsForTheLog)
{
    using Bots::NoDamageTarget;
    EXPECT_STREQ(Bots::NoDamageTargetText(NoDamageTarget::None), "");
    for (NoDamageTarget why : {NoDamageTarget::NoFocus, NoDamageTarget::TankHoldsNoThreat,
             NoDamageTarget::TankDeadNoLooseEnemy, NoDamageTarget::WaitForTaunt, NoDamageTarget::BeyondChaseLimit,
             NoDamageTarget::FocusFrozen, NoDamageTarget::HoldsBackOnThreat, NoDamageTarget::InOtherPacksReach})
        EXPECT_STRNE(Bots::NoDamageTargetText(why), "") << int(why);
}

// Code review of the no-target log: the dead tank holds no threat, and no
// enemy is loose. The reason says so, not that the tank holds no threat yet.
TEST(DamageTarget, WithTheTankDeadAndNoLooseEnemyTheReasonSaysSo)
{
    DamageBot bot;
    bot.fake.SetHealth(Tank, 0, 600);
    bot.group.SetTank(Tank);
    bot.group.StartPull(Tank, Wolf);
    bot.group.SetFocusTarget(Wolf);
    BotContext const context{bot.fake, bot.fake, bot.group, Bots::CommandState::Follow, 0x1};

    EXPECT_EQ(Bots::PickDamageTarget(context), NoGuid);
    EXPECT_EQ(Bots::WhyNoDamageTarget(context, false), Bots::NoDamageTarget::TankDeadNoLooseEnemy);
}

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

TEST(DamageTarget, LeavesAnEnemyOnAGroupMemberWhileTheTankPulls)
{
    DamageBot bot;
    Bots::Guid const Healer = 0x12;
    bot.fake.SetHealth(Tank, 600, 600);
    bot.fake.SetHealth(Healer, 300, 300);
    bot.fake.SetGroupMembers({Tank, Healer});
    bot.group.SetTank(Tank);
    bot.group.StartPull(Tank, Kobold);
    bot.fake.SetVictim(Wolf, Healer);

    EXPECT_EQ(bot.Target(), NoGuid);
}

// jvbastel/WOWAI#171: the chase limit counts from the tank only. A focus
// that the tank gave up on stays near the healer it hits, 30 yards from the
// tank: the bot does not go out to it.
TEST(DamageTarget, TheChaseLimitCountsFromTheTankOnly)
{
    DamageBot bot;
    Bots::Guid const Healer = 0x12;
    bot.fake.SetHealth(Tank, 600, 600);
    bot.fake.SetHealth(Healer, 300, 300);
    bot.fake.SetGroupMembers({Tank, Healer});
    bot.group.SetTank(Tank);
    bot.group.SetFocusTarget(Wolf);
    bot.group.GiveUpTaunt(Wolf);
    bot.fake.SetVictim(Wolf, Healer);
    bot.fake.SetDistanceBetween(Wolf, Tank, 30.0f);
    bot.fake.SetDistanceBetween(Wolf, Healer, 3.0f);

    EXPECT_EQ(bot.Target(), NoGuid);

    bot.fake.SetDistanceBetween(Wolf, Tank, Bots::ChaseEnemyYards);

    EXPECT_EQ(bot.Target(), Wolf);
}

// The bot waits for the taunt on the focus, and leaves another enemy on a
// member to the tank too (jvbastel/WOWAI#171).
TEST(DamageTarget, LeavesAnotherEnemyOnAMemberWhileItWaitsForTheTaunt)
{
    DamageBot bot;
    Bots::Guid const Healer = 0x12;
    bot.fake.SetHealth(Tank, 600, 600);
    bot.fake.SetHealth(Healer, 300, 300);
    bot.fake.SetGroupMembers({Tank, Healer});
    bot.group.SetTank(Tank);
    bot.group.SetFocusTarget(Wolf);
    bot.fake.SetVictim(Wolf, Healer);
    bot.fake.SetVictim(Kobold, Healer);

    EXPECT_EQ(bot.Target(), NoGuid);
}

// jvbastel/WOWAI#171: the focus that the tank holds comes before a loose
// enemy on a member. Peeling it makes more loose enemies; the tank picks it
// up.
TEST(DamageTarget, TheFocusComesBeforeALooseEnemyOnAMember)
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
    bot.Target(NoGuid, 0);

    EXPECT_EQ(bot.Target(NoGuid, Bots::HoldContactMs), Kobold);
}

// With no living tank, nobody picks up a loose enemy: the damage bots
// spread over them (WOWAI#93). Two loose enemies: the bot takes the one that no other member attacks, so
// that no enemy stays lost.
TEST(DamageTarget, TakesTheLooseEnemyThatNoOtherMemberAttacks)
{
    DamageBot bot;
    Bots::Guid const Healer = 0x12;
    Bots::Guid const Mage = 0x14;
    Bots::Guid const Trogg = 0xF130000302000044;
    bot.fake.SetHealth(Tank, 0, 600);
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
    bot.fake.SetHealth(Tank, 0, 600);
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
    bot.fake.SetHealth(Tank, 0, 600);
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
            bot->fake.SetHealth(unit, unit == Tank ? 0 : 300, 300);
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
// member is left to the tank too (jvbastel/WOWAI#171).
TEST(DamageTarget, LeavesALooseEnemyWhileTheRangedPulledFocusRunsToTheTank)
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

    EXPECT_EQ(bot.Target(NoGuid, 2000), NoGuid);
}

// jvbastel/WOWAI#171: a melee bot at the tank may hit its own attacker: the
// enemy is where the tank reaches it. Away from the tank, or with a focus to
// hit, it leaves the enemy to the tank.
TEST(DamageTarget, AMeleeBotAtTheTankHitsItsOwnAttacker)
{
    DamageBot bot;
    bot.fake.SetHealth(Tank, 600, 600);
    bot.group.SetTank(Tank);
    bot.group.StartPull(Tank, Kobold);
    bot.fake.SetVictim(Wolf, Rogue);
    BotContext context{bot.fake, bot.fake, bot.group, Bots::CommandState::Follow, 0x1};

    bot.fake.SetDistance(Tank, Bots::AtTankYards + 2.0f);
    EXPECT_EQ(Bots::PickMeleeDamageTarget(context), NoGuid);

    bot.fake.SetDistance(Tank, Bots::AtTankYards);
    EXPECT_EQ(Bots::PickMeleeDamageTarget(context), Wolf);
    // Not a ranged bot: it walks to the tank and waits for the focus.
    EXPECT_EQ(Bots::PickDamageTarget(context), NoGuid);
}

TEST(DamageTarget, AMeleeBotAtTheTankHitsTheFocusBeforeItsOwnAttacker)
{
    DamageBot bot;
    bot.fake.SetHealth(Tank, 600, 600);
    bot.group.SetTank(Tank);
    bot.HoldThePull();
    bot.fake.SetVictim(Wolf, Rogue);
    bot.fake.SetDistance(Tank, 3.0f);
    BotContext context{bot.fake, bot.fake, bot.group, Bots::CommandState::Follow, 0x1};
    context.nowMs = Bots::HoldContactMs;

    EXPECT_EQ(Bots::PickMeleeDamageTarget(context), Kobold);
}

// jvbastel/WOWAI#171: a damage bot that an enemy attacks walks to the tank,
// so that the tank can take the enemy off it, and keeps hitting the focus
// once there. Through the whole stack of a ranged and a melee bot.
namespace
{
    std::string Deed(char const* verb, Bots::Guid unit)
    {
        char text[64];
        std::snprintf(text, sizeof(text), "%s 0x%016llx", verb, static_cast<unsigned long long>(unit));
        return text;
    }

    std::string CastOn(Bots::SpellId spell, Bots::Guid target)
    {
        char text[64];
        std::snprintf(text, sizeof(text), "cast %u on 0x%016llx", spell, static_cast<unsigned long long>(target));
        return text;
    }

    // A damage bot in a fight: the tank holds the kobold, the focus, and
    // the wolf of an extra pack attacks the bot.
    struct AttackedBot
    {
        Bots::Guid self;
        FakeBot fake;
        GroupCoordinator group;
        Bots::StrategyStack stack;

        AttackedBot(Bots::Guid guid, Bots::BotClass botClass, Bots::BotRole role)
            : self(guid), fake(guid), stack(Bots::MakeStrategyStack(botClass, role))
        {
            fake.SetHealth(self, 400, 400);
            fake.SetPower(self, 600, 600);
            fake.SetHealth(Tank, 600, 600);
            fake.SetHealth(Kobold, 200, 200);
            fake.SetHealth(Wolf, 200, 200);
            fake.SetGroupMembers({Tank, self});
            fake.SetInCombat(self);
            group.SetTank(Tank);
            group.StartPull(Tank, Kobold);
            Bots::Tests::HoldThreat(group, Tank);
            fake.SetVictim(Kobold, Tank);
            fake.SetDistanceBetween(Kobold, Tank, 3.0f);
            fake.SetVictim(Wolf, self);
            fake.SetDistance(Wolf, 2.0f);
        }

        std::vector<std::string> Think()
        {
            fake.ClearDeeds();
            BotContext const context{fake, fake, group, Bots::CommandState::None, NoGuid};
            stack.Think(context);
            return fake.Deeds();
        }
    };

    struct AttackedMage : AttackedBot
    {
        AttackedMage() : AttackedBot(0x21, Bots::BotClass::Mage, Bots::BotRole::Ranged)
        {
            fake.SetKnownSpell(Bots::Mage::Frostbolt);
            fake.SetDistance(Kobold, 20.0f);
        }
    };
}

TEST(DamageBotAttacked, ARangedBotWalksToTheTank)
{
    AttackedMage mage;
    mage.fake.SetDistance(Tank, 20.0f);

    EXPECT_EQ(mage.Think(), (std::vector<std::string>{Deed("follow", Tank)}));
}

TEST(DamageBotAttacked, ARangedBotAtTheTankKeepsHittingTheFocus)
{
    AttackedMage mage;
    mage.fake.SetDistance(Tank, Bots::AtTankYards);

    EXPECT_EQ(mage.Think(), (std::vector<std::string>{CastOn(Bots::Mage::Frostbolt, Kobold)}));
}

TEST(DamageBotAttacked, ARangedBotNotAttackedStaysAtItsPlace)
{
    AttackedMage mage;
    mage.fake.SetDistance(Tank, 20.0f);
    mage.fake.SetVictim(Wolf, NoGuid);

    EXPECT_EQ(mage.Think(), (std::vector<std::string>{CastOn(Bots::Mage::Frostbolt, Kobold)}));
}

// With no living tank, nobody takes the enemy off the bot: it fights back
// where it stands.
TEST(DamageBotAttacked, NoWalkWhenTheTankIsDead)
{
    AttackedMage mage;
    mage.fake.SetDistance(Tank, 20.0f);
    mage.fake.SetHealth(Tank, 0, 600);

    std::vector<std::string> const deeds = mage.Think();

    ASSERT_EQ(deeds.size(), 1u);
    EXPECT_NE(deeds[0], Deed("follow", Tank));
}

TEST(DamageBotAttacked, AMeleeBotAwayFromTheTankWalksToIt)
{
    AttackedBot rogue(Rogue, Bots::BotClass::Rogue, Bots::BotRole::Melee);
    rogue.fake.SetDistance(Tank, 20.0f);

    EXPECT_EQ(rogue.Think(), (std::vector<std::string>{Deed("follow", Tank)}));
}

// Final review of jvbastel/WOWAI#165, item 2: since #173 the pull stays held
// after the focus dies. The pull's exception (the damage bots hit the pulled
// target on a member) is for the pull target only: a next focus on the healer
// waits for the tank's taunt, or for the tank to give it up.
TEST(DamageTarget, ANextFocusInAHeldPullWaitsForItsTaunt)
{
    DamageBot bot;
    Bots::Guid const Healer = 0x12;
    bot.fake.SetHealth(Tank, 600, 600);
    bot.group.SetTank(Tank);
    bot.HoldThePull();
    bot.fake.SetHealth(Kobold, 0, 200);
    bot.fake.SetVictim(Kobold, NoGuid);
    bot.group.SetFocusTarget(Wolf);
    bot.fake.SetVictim(Wolf, Healer);
    bot.fake.SetDistanceBetween(Wolf, Tank, 3.0f);
    std::int64_t const later = Bots::HoldLimitMs + 2000;

    EXPECT_EQ(bot.Target(NoGuid, later), NoGuid);

    bot.fake.SetVictim(Wolf, Tank);

    EXPECT_EQ(bot.Target(NoGuid, later), Wolf);
}

TEST(DamageTarget, ANextFocusInAHeldPullWhenNoTauntComes)
{
    DamageBot bot;
    Bots::Guid const Healer = 0x12;
    bot.fake.SetHealth(Tank, 600, 600);
    bot.group.SetTank(Tank);
    bot.HoldThePull();
    bot.fake.SetHealth(Kobold, 0, 200);
    bot.fake.SetVictim(Kobold, NoGuid);
    bot.group.SetFocusTarget(Wolf);
    bot.fake.SetVictim(Wolf, Healer);
    bot.fake.SetDistanceBetween(Wolf, Tank, 3.0f);
    bot.group.GiveUpTaunt(Wolf);

    EXPECT_EQ(bot.Target(NoGuid, Bots::HoldLimitMs + 2000), Wolf);
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

// While the tank lives, the damage bots leave a loose enemy to it
// (jvbastel/WOWAI#171); with no living tank, they take it once the freeze
// ends.
TEST(DamageTarget, AttacksTheLooseEnemyOnceTheFreezeEnds)
{
    FrozenWolfOnTheHealer bot;
    bot.fake.SetHealth(Tank, 0, 600);
    EXPECT_NE(bot.Target(), Wolf);
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
    bot.fake.SetHealth(Tank, 0, 600);
    EXPECT_NE(bot.MeleeTarget(), Wolf);
    bot.fake.SetAura(Wolf, Bots::Mage::FrostNova, false);

    EXPECT_EQ(bot.MeleeTarget(), Wolf);
}

// At the tank, with no focus to hit, a melee bot hits its own attacker
// (jvbastel/WOWAI#171), but not a frozen one.
TEST(DamageTarget, MeleeDamageHitsBackAnAttackerThatIsNotFrozen)
{
    FrozenWolfOnTheHealer bot;
    bot.fake.SetAura(Wolf, Bots::Mage::FrostNova, false);
    bot.fake.SetGroupMembers({Tank, FrozenHealer, Rogue});
    bot.fake.SetVictim(Wolf, Rogue);
    bot.fake.SetHealth(Kobold, 0, 200);
    bot.fake.SetDistance(Tank, 3.0f);

    EXPECT_EQ(bot.MeleeTarget(), Wolf);
}

TEST(DamageTarget, MeleeDamageDoesNotHitBackAFrozenAttacker)
{
    FrozenWolfOnTheHealer bot;
    bot.fake.SetGroupMembers({Tank, FrozenHealer, Rogue});
    bot.fake.SetVictim(Wolf, Rogue);
    bot.fake.SetHealth(Kobold, 0, 200);
    bot.fake.SetDistance(Tank, 3.0f);

    EXPECT_EQ(bot.MeleeTarget(), NoGuid);
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
