#include "DamageRules.h"
#include "FakeBot.h"
#include "GroupCoordinator.h"
#include "PlayerTank.h"

#include <gtest/gtest.h>

using Bots::BotContext;
using Bots::GroupCoordinator;
using Bots::NoGuid;
using Bots::Tests::FakeBot;

// jvbastel/WOWAI#194: the threat throttle. In a player-tank group a damage
// bot holds back on an enemy at 90% of the threat that would take the enemy
// from the tank (110% of the tank's threat in melee, 130% at range), and
// starts again under 80% of it.
namespace
{
    Bots::Guid const Human = 0x1;
    Bots::Guid const Healer = 0x12;
    Bots::Guid const Self = 0x13;
    Bots::Guid const Kobold = 0xF130000302000022;
    Bots::Guid const Wolf = 0xF130000302000033;

    // A damage bot in a player-tank group. The tank has held the kobold
    // for the threat lead (PlayerTankHoldMs).
    struct ThrottledBot
    {
        FakeBot fake{Self};
        GroupCoordinator group;
        std::int64_t nowMs = Bots::PlayerTankHoldMs;

        // `held`: the tank has held the kobold for the threat lead.
        explicit ThrottledBot(bool held = true)
        {
            fake.SetGroupMembers({Human, Healer});
            fake.SetHealth(Human, 600, 600);
            fake.SetHealth(Healer, 300, 300);
            fake.SetHealth(Self, 400, 400);
            fake.SetHealth(Kobold, 200, 200);
            fake.SetHealth(Wolf, 200, 200);
            fake.SetDistance(Human, 3.0f);
            group.SetTank(Human, true);
            fake.SetVictim(Kobold, Human);
            fake.SetThreatRatio(Human, Kobold, 1.5f);
            fake.SetTargetOf(Human, Kobold);
            Bots::WatchPlayerTank(fake, group, 0);
            if (held)
                Bots::WatchPlayerTank(fake, group, nowMs);
            else
                nowMs = 0;
        }

        // The bot's threat on the kobold, as a ratio of the tank's.
        void Threat(float ratio) { fake.SetThreatRatio(Self, Kobold, ratio); }

        BotContext Context()
        {
            BotContext context{fake, fake, group, Bots::CommandState::Follow, Human, NoGuid};
            context.nowMs = nowMs;
            return context;
        }

        Bots::Guid Melee()
        {
            nowMs += 100;
            return Bots::PickMeleeDamageTarget(Context());
        }

        Bots::Guid Ranged()
        {
            nowMs += 100;
            return Bots::PickDamageTarget(Context());
        }
    };
}

TEST(ThreatThrottle, AMeleeBotHoldsBackAt90PercentOfTheMeleeTakeover)
{
    ThrottledBot bot;
    bot.Threat(0.98f);
    EXPECT_EQ(bot.Melee(), Kobold);
    bot.Threat(1.0f);
    EXPECT_EQ(bot.Melee(), NoGuid);
    // Between 80% and 90% of the takeover: it still holds back.
    bot.Threat(0.9f);
    EXPECT_EQ(bot.Melee(), NoGuid);
    bot.Threat(0.87f);
    EXPECT_EQ(bot.Melee(), Kobold);
    bot.Threat(0.95f);
    EXPECT_EQ(bot.Melee(), Kobold);
}

TEST(ThreatThrottle, ARangedBotHoldsBackAt90PercentOfTheRangedTakeover)
{
    ThrottledBot bot;
    bot.Threat(1.16f);
    EXPECT_EQ(bot.Ranged(), Kobold);
    bot.Threat(1.18f);
    EXPECT_EQ(bot.Ranged(), NoGuid);
    bot.Threat(1.1f);
    EXPECT_EQ(bot.Ranged(), NoGuid);
    bot.Threat(1.03f);
    EXPECT_EQ(bot.Ranged(), Kobold);
}

TEST(ThreatThrottle, ABotTankGroupHasNoThrottle)
{
    ThrottledBot bot;
    bot.group.SetTank(Human, false);
    bot.group.StartPull(Human, Kobold);
    Bots::Tests::HoldThreat(bot.group, Human, bot.nowMs);
    bot.Threat(1.25f);
    EXPECT_EQ(bot.Melee(), Kobold);
}

// Story 35: the damage bots never break the threat wait for danger, but a
// loose enemy that attacks a bot at the tank gets fought back, as today.
TEST(ThreatThrottle, AMeleeBotFightsBackALooseEnemyBeforeTheThreatLead)
{
    ThrottledBot bot(false);
    ASSERT_FALSE(bot.group.TankHoldsThreat(bot.nowMs + 100));
    EXPECT_EQ(bot.Melee(), NoGuid);

    bot.fake.SetVictim(Wolf, Self);
    bot.fake.SetInCombat(Wolf);
    EXPECT_EQ(bot.Melee(), Wolf);
}

// Review of jvbastel/WOWAI#194: the throttle holds on every attack in a
// player-tank group, an enemy healer too. A hold on one enemy leaves the
// other: the bot goes on to the focus while it holds on the healer.
TEST(ThreatThrottle, ABotHoldsBackOnAnEnemyHealerToo)
{
    Bots::Guid const Shaman = 0xF130000302000044;
    ThrottledBot bot;
    bot.fake.SetHealth(Shaman, 200, 200);
    bot.fake.SetVictim(Shaman, Human);
    bot.group.MarkEnemyHealer(Shaman);
    bot.Threat(0.5f);
    bot.fake.SetThreatRatio(Self, Shaman, 0.98f);
    EXPECT_EQ(bot.Melee(), Shaman);
    bot.fake.SetThreatRatio(Self, Shaman, 1.0f);
    EXPECT_EQ(bot.Melee(), Kobold);
    bot.fake.SetThreatRatio(Self, Shaman, 0.9f);
    EXPECT_EQ(bot.Melee(), Kobold);
    bot.fake.SetThreatRatio(Self, Shaman, 0.87f);
    EXPECT_EQ(bot.Melee(), Shaman);
}

TEST(ThreatThrottle, ABotHoldsBackOnTheHealerAndTheFocusAtOnce)
{
    Bots::Guid const Shaman = 0xF130000302000044;
    ThrottledBot bot;
    bot.fake.SetHealth(Shaman, 200, 200);
    bot.fake.SetVictim(Shaman, Human);
    bot.group.MarkEnemyHealer(Shaman);
    bot.fake.SetThreatRatio(Self, Shaman, 1.0f);
    bot.Threat(1.0f);
    EXPECT_EQ(bot.Melee(), NoGuid);
    // Both still between 80% and 90% of the takeover: both holds stay.
    bot.fake.SetThreatRatio(Self, Shaman, 0.9f);
    bot.Threat(0.9f);
    EXPECT_EQ(bot.Melee(), NoGuid);
    bot.Threat(0.5f);
    EXPECT_EQ(bot.Melee(), Kobold);
}

TEST(ThreatThrottle, AHoldOnADeadEnemyGoesWithTheNextHold)
{
    Bots::Guid const Shaman = 0xF130000302000044;
    ThrottledBot bot;
    bot.fake.SetHealth(Shaman, 200, 200);
    bot.fake.SetVictim(Shaman, Human);
    bot.group.MarkEnemyHealer(Shaman);
    bot.fake.SetThreatRatio(Self, Shaman, 1.0f);
    EXPECT_EQ(bot.Melee(), Kobold);
    bot.fake.SetHealth(Shaman, 0, 200);
    bot.Threat(1.0f);
    EXPECT_EQ(bot.Melee(), NoGuid);
    EXPECT_EQ(bot.group.GetThreatHolds(Self), std::vector<Bots::Guid>{Kobold});
}
