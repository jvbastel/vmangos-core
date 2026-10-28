#include "BossFights.h"
#include "FakeBot.h"

#include <gtest/gtest.h>

using Bots::GroupCoordinator;
using Bots::GroupEvent;
using Bots::GroupEventKind;
using Bots::LifeState;
using Bots::NoGuid;
using Bots::TapWatch;
using Bots::Tests::FakeBot;

namespace
{
    Bots::Guid const Tank = 0x11;
    Bots::Guid const Bazzalan = 0xF130002C7F000001;
    std::uint32_t const BazzalanEntry = 11519;

    // The tank fights Bazzalan, a boss of the route file, with the tap.
    struct TappingTank
    {
        FakeBot fake{Tank};
        GroupCoordinator group;
        TapWatch watch;
        std::int64_t nowMs = 1000;

        TappingTank()
        {
            fake.SetHealth(Tank, 600, 600);
            fake.SetEntry(Bazzalan, BazzalanEntry);
            fake.SetListedBoss(Bazzalan);
            fake.SetTap(Bazzalan);
            fake.SetHealth(Bazzalan, 4000, 9000);
            fake.SetVictim(Tank, Bazzalan);
            fake.SetVictim(Bazzalan, Tank);
        }

        void Update() { watch.Update(fake, group, nowMs += 500); }

        void BossDies()
        {
            fake.SetHealth(Bazzalan, 0, 9000);
            fake.SetLifeState(Bazzalan, LifeState::Dead);
            fake.SetVictim(Tank, NoGuid);
            fake.SetVictim(Bazzalan, NoGuid);
        }
    };
}

// jvbastel/WOWAI#54: the member with the tap gets no SMSG_PARTYKILLLOG.
TEST(TapWatch, TheBotWithTheTapNotesTheBossKill)
{
    TappingTank bot;
    bot.Update();
    bot.BossDies();
    bot.Update();

    std::vector<GroupEvent> const events = bot.group.TakeEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].kind, GroupEventKind::BossKill);
    EXPECT_EQ(events[0].target, Bazzalan);
    EXPECT_EQ(events[0].entry, BazzalanEntry);
}

TEST(TapWatch, ABotWithoutTheTapNotesNoKill)
{
    TappingTank bot;
    bot.fake.SetTap(Bazzalan, false);
    bot.Update();
    bot.BossDies();
    bot.Update();

    EXPECT_TRUE(bot.group.TakeEvents().empty());
}

TEST(TapWatch, TheKillAndThePacketOfAnotherBotMakeOneEvent)
{
    TappingTank bot;
    bot.Update();
    bot.BossDies();
    bot.group.NoteBossKill(Bazzalan, BazzalanEntry, bot.nowMs); // Another bot saw SMSG_PARTYKILLLOG.
    bot.Update();

    EXPECT_EQ(bot.group.TakeEvents().size(), 1u);
}

TEST(TapWatch, ABossThatEvadesIsNoKill)
{
    TappingTank bot;
    bot.Update();
    bot.fake.SetVictim(Tank, NoGuid);
    bot.fake.SetVictim(Bazzalan, NoGuid); // Out of combat, alive.
    bot.Update();
    bot.BossDies(); // Later, with no fight of this bot.
    bot.Update();

    EXPECT_TRUE(bot.group.TakeEvents().empty());
}
