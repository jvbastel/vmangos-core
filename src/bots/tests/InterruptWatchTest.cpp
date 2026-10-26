#include "InterruptWatch.h"

#include <gtest/gtest.h>

using Bots::CastKind;
using Bots::EnemyCastSeen;
using Bots::InterruptWatch;

// The interrupts of a scripted run (spec section 10, Results): done, missed,
// and double.
namespace
{
    Bots::Guid const Rogue = 0x13;
    Bots::Guid const Tank = 0x11;
    Bots::Guid const Shaman = 0xF130000302000022;
    Bots::Guid const Other = 0xF130000302000033;
    Bots::SpellId const HealingWave = 332;
    Bots::SpellId const LightningBolt = 403;

    EnemyCastSeen HealOf(Bots::Guid enemy)
    {
        return {enemy, true, HealingWave, CastKind::Heal};
    }

    EnemyCastSeen Idle(Bots::Guid enemy)
    {
        return {enemy, true, Bots::NoSpell, CastKind::None};
    }
}

TEST(InterruptWatch, ALandedInterruptIsDone)
{
    InterruptWatch watch;
    watch.Observe({HealOf(Shaman)}, true, 1000);
    watch.NoteAttempt(Rogue, Shaman, HealingWave, 1500);
    watch.NoteLanded(Rogue, Shaman, HealingWave, CastKind::Heal, 1600);
    watch.Observe({Idle(Shaman)}, true, 1700);
    watch.Observe({Idle(Shaman)}, true, 9000);

    ASSERT_EQ(watch.Done().size(), 1u);
    EXPECT_EQ(watch.Done()[0].member, Rogue);
    EXPECT_EQ(watch.Done()[0].enemy, Shaman);
    EXPECT_EQ(watch.Done()[0].spell, HealingWave);
    EXPECT_EQ(watch.Done()[0].kind, CastKind::Heal);
    EXPECT_EQ(watch.Done()[0].ms, 1600);
    EXPECT_TRUE(watch.Missed().empty());
    EXPECT_TRUE(watch.Doubles().empty());
}

TEST(InterruptWatch, ACastThatEndsWithoutAnInterruptWhileAnInterrupterIsFreeIsMissed)
{
    InterruptWatch watch;
    watch.Observe({HealOf(Shaman)}, true, 1000);
    watch.Observe({HealOf(Shaman)}, true, 2000);
    watch.Observe({Idle(Shaman)}, true, 3000);
    // The note of a landed interrupt can come a little later than the end
    // of the cast: the miss waits for it.
    EXPECT_TRUE(watch.Missed().empty());
    watch.Observe({Idle(Shaman)}, true, 3000 + Bots::InterruptNoteGraceMs + 1);

    ASSERT_EQ(watch.Missed().size(), 1u);
    EXPECT_EQ(watch.Missed()[0].enemy, Shaman);
    EXPECT_EQ(watch.Missed()[0].spell, HealingWave);
    EXPECT_EQ(watch.Missed()[0].kind, CastKind::Heal);
    EXPECT_EQ(watch.Missed()[0].ms, 1000);
    EXPECT_TRUE(watch.Done().empty());
}

TEST(InterruptWatch, ALateNoteOfALandedInterruptIsNoMiss)
{
    InterruptWatch watch;
    watch.Observe({HealOf(Shaman)}, true, 1000);
    watch.Observe({Idle(Shaman)}, true, 2000);
    watch.NoteLanded(Rogue, Shaman, HealingWave, CastKind::Heal, 2100);
    watch.Observe({Idle(Shaman)}, true, 9000);

    EXPECT_TRUE(watch.Missed().empty());
    EXPECT_EQ(watch.Done().size(), 1u);
}

TEST(InterruptWatch, ACastWhileNoInterrupterIsFreeIsNoMiss)
{
    InterruptWatch watch;
    watch.Observe({HealOf(Shaman)}, false, 1000);
    watch.Observe({HealOf(Shaman)}, false, 2000);
    watch.Observe({Idle(Shaman)}, false, 3000);
    watch.Observe({Idle(Shaman)}, false, 9000);

    EXPECT_TRUE(watch.Missed().empty());
}

TEST(InterruptWatch, AnInterrupterFreeAtAnyUpdateOfTheCastCounts)
{
    InterruptWatch watch;
    watch.Observe({HealOf(Shaman)}, false, 1000);
    watch.Observe({HealOf(Shaman)}, true, 2000);
    watch.Observe({Idle(Shaman)}, false, 3000);
    watch.Observe({Idle(Shaman)}, false, 9000);

    EXPECT_EQ(watch.Missed().size(), 1u);
}

TEST(InterruptWatch, ACasterThatDiesMissesNothing)
{
    InterruptWatch watch;
    watch.Observe({HealOf(Shaman)}, true, 1000);
    watch.Observe({{Shaman, false, Bots::NoSpell, CastKind::None}}, true, 2000);
    watch.Observe({}, true, 9000);

    EXPECT_TRUE(watch.Missed().empty());
}

TEST(InterruptWatch, ACasterThatGoesAwayMissesNothing)
{
    InterruptWatch watch;
    watch.Observe({HealOf(Shaman)}, true, 1000);
    watch.Observe({}, true, 2000);
    watch.Observe({}, true, 9000);

    EXPECT_TRUE(watch.Missed().empty());
}

TEST(InterruptWatch, ANewSpellEndsTheCastBefore)
{
    InterruptWatch watch;
    watch.Observe({HealOf(Shaman)}, true, 1000);
    watch.Observe({{Shaman, true, LightningBolt, CastKind::Harm}}, true, 2000);
    watch.Observe({Idle(Shaman)}, true, 4000);
    watch.Observe({Idle(Shaman)}, true, 9000);

    ASSERT_EQ(watch.Missed().size(), 2u);
    EXPECT_EQ(watch.Missed()[0].spell, HealingWave);
    EXPECT_EQ(watch.Missed()[1].spell, LightningBolt);
    EXPECT_EQ(watch.Missed()[1].kind, CastKind::Harm);
}

TEST(InterruptWatch, EachCasterHasItsOwnCast)
{
    InterruptWatch watch;
    watch.Observe({HealOf(Shaman), HealOf(Other)}, true, 1000);
    watch.NoteLanded(Rogue, Shaman, HealingWave, CastKind::Heal, 1500);
    watch.Observe({Idle(Shaman), Idle(Other)}, true, 2000);
    watch.Observe({Idle(Shaman), Idle(Other)}, true, 9000);

    ASSERT_EQ(watch.Missed().size(), 1u);
    EXPECT_EQ(watch.Missed()[0].enemy, Other);
}

TEST(InterruptWatch, AnAttemptAfterTheLandedInterruptOfAnotherBotIsADouble)
{
    InterruptWatch watch;
    watch.Observe({HealOf(Shaman)}, true, 1000);
    watch.NoteAttempt(Tank, Shaman, HealingWave, 1500);
    watch.NoteAttempt(Rogue, Shaman, HealingWave, 1600);
    watch.NoteLanded(Tank, Shaman, HealingWave, CastKind::Heal, 1800);
    watch.Observe({Idle(Shaman)}, true, 2000);
    watch.Observe({Idle(Shaman)}, true, 9000);

    ASSERT_EQ(watch.Doubles().size(), 1u);
    EXPECT_EQ(watch.Doubles()[0].enemy, Shaman);
    EXPECT_EQ(watch.Doubles()[0].spell, HealingWave);
    EXPECT_EQ(watch.Doubles()[0].first, Tank);
    EXPECT_EQ(watch.Doubles()[0].second, Rogue);
    EXPECT_EQ(watch.Doubles()[0].ms, 1600);
}

TEST(InterruptWatch, ASecondTryAfterAMissedKickIsNoDouble)
{
    // The Kick of the rogue misses (a dodge), so the tank bashes.
    InterruptWatch watch;
    watch.Observe({HealOf(Shaman)}, true, 1000);
    watch.NoteAttempt(Rogue, Shaman, HealingWave, 1200);
    watch.NoteAttempt(Tank, Shaman, HealingWave, 1700);
    watch.NoteLanded(Tank, Shaman, HealingWave, CastKind::Heal, 1900);
    watch.Observe({Idle(Shaman)}, true, 2000);
    watch.Observe({Idle(Shaman)}, true, 9000);

    EXPECT_TRUE(watch.Doubles().empty());
    EXPECT_EQ(watch.Done().size(), 1u);
}

TEST(InterruptWatch, AnAttemptAtALaterCastIsNoDouble)
{
    InterruptWatch watch;
    watch.NoteAttempt(Tank, Shaman, HealingWave, 1000);
    watch.NoteLanded(Tank, Shaman, HealingWave, CastKind::Heal, 1100);
    watch.NoteAttempt(Rogue, Shaman, HealingWave, 1100 + Bots::DoubleInterruptMs + 1);

    EXPECT_TRUE(watch.Doubles().empty());
}
