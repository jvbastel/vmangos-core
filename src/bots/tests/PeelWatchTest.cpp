#include "PeelWatch.h"

#include <gtest/gtest.h>

using Bots::PeelEnemySeen;
using Bots::PeelSeen;
using Bots::PeelWatch;

// The result facts of the peel nova and the step back (jvbastel/WOWAI#145,
// spec #142): the novas cast for others, the freezes that party damage
// broke before the tank arrived, and where the healer ends.
namespace
{
    Bots::Guid const Tank = 0x11;
    Bots::Guid const Healer = 0x12;
    Bots::Guid const Warlock = 0x14;
    Bots::Guid const Mage = 0x15;
    Bots::Guid const Enforcer = 0xF130000302000022;
    Bots::Guid const Cultist = 0xF130000302000023;

    // The enemy attacks `victim` from `yards` (between the edges).
    PeelEnemySeen On(Bots::Guid enemy, Bots::Guid victim, float yards, std::uint32_t health = 1000)
    {
        PeelEnemySeen seen;
        seen.enemy = enemy;
        seen.victim = victim;
        seen.victimYards = yards;
        seen.health = health;
        seen.healerYards = yards;
        return seen;
    }

    PeelEnemySeen Frozen(PeelEnemySeen seen, Bots::Guid by = Mage)
    {
        seen.frozenBy = by;
        return seen;
    }

    PeelEnemySeen TankHas(PeelEnemySeen seen)
    {
        seen.tankHasIt = true;
        return seen;
    }

    PeelEnemySeen HealerAt(PeelEnemySeen seen, float yards)
    {
        seen.healerYards = yards;
        return seen;
    }

    PeelSeen Seen(std::vector<PeelEnemySeen> enemies, float healerTankYards = 20.0f, bool healerSeesTank = true)
    {
        PeelSeen seen;
        seen.tank = Tank;
        seen.healer = Healer;
        seen.healerTankYards = healerTankYards;
        seen.healerSeesTank = healerSeesTank;
        seen.enemies = enemies;
        return seen;
    }

    // An enemy reaches the healer at 1 s, the Mage freezes it at 2.2 s, the
    // healer steps back, and the tank takes it at 4 s.
    PeelWatch AGoodPeel()
    {
        PeelWatch watch;
        watch.Update(Seen({On(Enforcer, Healer, 20.0f)}), 0);
        watch.Update(Seen({On(Enforcer, Healer, 2.0f)}), 1000);
        watch.Update(Seen({Frozen(On(Enforcer, Healer, 2.0f))}), 2200);
        watch.Update(Seen({HealerAt(Frozen(On(Enforcer, Healer, 9.0f)), 10.0f)}), 3000);
        watch.Update(Seen({TankHas(HealerAt(Frozen(On(Enforcer, Healer, 9.0f)), 10.0f))}), 4000);
        watch.Update(Seen({TankHas(HealerAt(On(Enforcer, Tank, 1.0f, 900), 10.0f))}), 4500);
        return watch;
    }
}

TEST(PeelWatch, ANovaOnTheHealersEnemyIsForOthersWithTheTimeSinceTheEnemyReachedTheHealer)
{
    PeelWatch const watch = AGoodPeel();

    ASSERT_EQ(watch.Novas().size(), 1u);
    Bots::PeelNova const& nova = watch.Novas()[0];
    EXPECT_EQ(nova.caster, Mage);
    EXPECT_EQ(nova.ms, 2200);
    EXPECT_EQ(nova.victims, std::vector<Bots::Guid>{Healer});
    EXPECT_TRUE(nova.forOthers);
    EXPECT_EQ(nova.healerReachMs, 1200);
    EXPECT_EQ(watch.NovasForOthers(), 1u);
}

TEST(PeelWatch, ANovaOnlyForTheMagesOwnEnemyIsNotForOthers)
{
    PeelWatch watch;
    watch.Update(Seen({On(Enforcer, Mage, 2.0f)}), 0);
    watch.Update(Seen({Frozen(On(Enforcer, Mage, 2.0f))}), 500);

    ASSERT_EQ(watch.Novas().size(), 1u);
    EXPECT_FALSE(watch.Novas()[0].forOthers);
    EXPECT_EQ(watch.Novas()[0].healerReachMs, -1);
    EXPECT_EQ(watch.NovasForOthers(), 0u);
}

TEST(PeelWatch, TheTanksOwnEnemiesInTheNovaAreNoVictims)
{
    PeelWatch watch;
    watch.Update(Seen({On(Enforcer, Warlock, 2.0f), On(Cultist, Tank, 1.0f)}), 0);
    watch.Update(Seen({Frozen(On(Enforcer, Warlock, 2.0f)), Frozen(On(Cultist, Tank, 1.0f))}), 1600);

    ASSERT_EQ(watch.Novas().size(), 1u);
    EXPECT_EQ(watch.Novas()[0].victims, std::vector<Bots::Guid>{Warlock});
    EXPECT_TRUE(watch.Novas()[0].forOthers);
    EXPECT_EQ(watch.Freezes().size(), 2u);
}

TEST(PeelWatch, EachNovaCountsOnceAndANewFreezeLaterIsANewNova)
{
    PeelWatch watch;
    watch.Update(Seen({On(Enforcer, Healer, 2.0f)}), 0);
    watch.Update(Seen({Frozen(On(Enforcer, Healer, 2.0f))}), 500);
    watch.Update(Seen({Frozen(On(Enforcer, Healer, 2.0f))}), 1000);
    watch.Update(Seen({On(Enforcer, Healer, 2.0f)}), 9000);
    watch.Update(Seen({Frozen(On(Enforcer, Healer, 2.0f))}), 30000);

    ASSERT_EQ(watch.Novas().size(), 2u);
    EXPECT_EQ(watch.Novas()[1].ms, 30000);
    ASSERT_EQ(watch.Freezes().size(), 2u);
    EXPECT_EQ(watch.Freezes()[0].startMs, 500);
    EXPECT_EQ(watch.Freezes()[0].endMs, 9000);
}

TEST(PeelWatch, PartyDamageThatEndsTheFreezeBeforeTheTankArrivesBreaksIt)
{
    PeelWatch watch;
    watch.Update(Seen({On(Enforcer, Healer, 2.0f)}), 0);
    watch.Update(Seen({Frozen(On(Enforcer, Healer, 2.0f))}), 500);
    watch.Update(Seen({Frozen(On(Enforcer, Healer, 2.0f, 950))}), 1500);
    watch.Update(Seen({On(Enforcer, Healer, 2.0f, 950)}), 2000);

    ASSERT_EQ(watch.Freezes().size(), 1u);
    EXPECT_TRUE(watch.Freezes()[0].brokenByParty);
    EXPECT_EQ(watch.Freezes()[0].tankMs, -1);
    EXPECT_EQ(watch.NovasBrokenByParty(), 1u);
}

TEST(PeelWatch, TheTankMayBreakTheFreezeWhenItArrives)
{
    PeelWatch const watch = AGoodPeel();

    ASSERT_EQ(watch.Freezes().size(), 1u);
    EXPECT_EQ(watch.Freezes()[0].tankMs, 4000);
    EXPECT_FALSE(watch.Freezes()[0].brokenByParty);
    EXPECT_EQ(watch.NovasBrokenByParty(), 0u);
}

TEST(PeelWatch, AFreezeThatRunsOutOrWhoseEnemyDiesIsNotBroken)
{
    PeelWatch watch;
    watch.Update(Seen({On(Enforcer, Healer, 2.0f), On(Cultist, Warlock, 2.0f)}), 0);
    watch.Update(Seen({Frozen(On(Enforcer, Healer, 2.0f)), Frozen(On(Cultist, Warlock, 2.0f))}), 500);
    // A DoT hits the Enforcer; its freeze runs out after 8 s. The Cultist dies.
    watch.Update(Seen({Frozen(On(Enforcer, Healer, 2.0f, 900))}), 4000);
    watch.Update(Seen({On(Enforcer, Healer, 2.0f, 900)}), 8500);

    ASSERT_EQ(watch.Freezes().size(), 2u);
    EXPECT_FALSE(watch.Freezes()[0].brokenByParty);
    EXPECT_FALSE(watch.Freezes()[1].brokenByParty);
    EXPECT_EQ(watch.NovasBrokenByParty(), 0u);
}

TEST(PeelWatch, TheHealerIsMeasuredWhereItStandsWhenTheFreezeOfItsEnemyEnds)
{
    PeelWatch const watch = AGoodPeel();

    Bots::PeelFreeze const& freeze = watch.Freezes()[0];
    EXPECT_TRUE(freeze.onHealer);
    EXPECT_EQ(freeze.endMs, 4500);
    EXPECT_FLOAT_EQ(freeze.healerYards, 10.0f);
    EXPECT_FLOAT_EQ(freeze.healerTankYards, 20.0f);
    EXPECT_TRUE(freeze.healerSeesTank);
}

TEST(PeelWatch, AGoodPeelPasses)
{
    EXPECT_EQ(Bots::PeelFailure(AGoodPeel()), "");
}

TEST(PeelWatch, ARunWithNoNovaOnTheHealersEnemyFails)
{
    PeelWatch watch;
    watch.Update(Seen({On(Enforcer, Healer, 2.0f)}), 0);
    watch.Update(Seen({TankHas(On(Enforcer, Tank, 1.0f))}), 3000);

    EXPECT_EQ(Bots::PeelFailure(watch), "no Frost Nova froze an enemy on the healer");
}

TEST(PeelWatch, ALateNovaFails)
{
    PeelWatch watch;
    watch.Update(Seen({On(Enforcer, Healer, 2.0f)}), 0);
    watch.Update(Seen({Frozen(On(Enforcer, Healer, 2.0f))}), 2600);
    watch.Update(Seen({TankHas(HealerAt(On(Enforcer, Tank, 1.0f), 10.0f))}), 5000);

    EXPECT_EQ(Bots::PeelFailure(watch), "the nova came 2.6 s after the enemy reached the healer (limit 2.0 s)");
}

TEST(PeelWatch, AFreezeBrokenByPartyDamageFails)
{
    PeelWatch watch;
    watch.Update(Seen({On(Enforcer, Healer, 2.0f)}), 0);
    watch.Update(Seen({Frozen(On(Enforcer, Healer, 2.0f))}), 500);
    watch.Update(Seen({HealerAt(On(Enforcer, Healer, 9.0f, 900), 10.0f)}), 1500);

    EXPECT_EQ(Bots::PeelFailure(watch), "party damage broke a freeze before the tank arrived");
}

TEST(PeelWatch, AHealerThatEndsNearTheFrozenEnemyFails)
{
    PeelWatch watch;
    watch.Update(Seen({On(Enforcer, Healer, 2.0f)}), 0);
    watch.Update(Seen({Frozen(On(Enforcer, Healer, 2.0f))}), 500);
    watch.Update(Seen({TankHas(HealerAt(On(Enforcer, Tank, 1.0f), 5.0f))}), 3000);

    EXPECT_EQ(Bots::PeelFailure(watch), "the healer ended 5.0 yd from the frozen enemy (at least 8 yd)");
}

TEST(PeelWatch, AHealerThatEndsOutOfReachOrSightOfTheTankFails)
{
    PeelWatch far;
    far.Update(Seen({On(Enforcer, Healer, 2.0f)}), 0);
    far.Update(Seen({Frozen(On(Enforcer, Healer, 2.0f))}), 500);
    far.Update(Seen({TankHas(HealerAt(On(Enforcer, Tank, 1.0f), 10.0f))}, 40.0f), 3000);
    PeelWatch blind;
    blind.Update(Seen({On(Enforcer, Healer, 2.0f)}), 0);
    blind.Update(Seen({Frozen(On(Enforcer, Healer, 2.0f))}), 500);
    blind.Update(Seen({TankHas(HealerAt(On(Enforcer, Tank, 1.0f), 10.0f))}, 20.0f, false), 3000);

    EXPECT_EQ(Bots::PeelFailure(far), "the healer ended 40.0 yd from the tank (at most 36 yd)");
    EXPECT_EQ(Bots::PeelFailure(blind), "the healer ended out of sight of the tank");
}
