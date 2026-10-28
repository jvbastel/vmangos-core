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

// Seen in game (run 2 of jvbastel/WOWAI#145): the damage of Frost Nova
// turns the enemy on the Mage in the same update that shows the freeze. The
// victim of the nova is the member that the enemy hit before it.
TEST(PeelWatch, TheVictimIsTheMemberThatTheEnemyHitBeforeTheFreeze)
{
    PeelWatch watch;
    watch.Update(Seen({On(Enforcer, Healer, 20.0f)}), 0);
    watch.Update(Seen({On(Enforcer, Healer, 2.0f)}), 3300);
    watch.Update(Seen({Frozen(On(Enforcer, Mage, 2.0f))}), 3600);

    ASSERT_EQ(watch.Novas().size(), 1u);
    EXPECT_EQ(watch.Novas()[0].victims, std::vector<Bots::Guid>{Healer});
    EXPECT_TRUE(watch.Novas()[0].forOthers);
    EXPECT_EQ(watch.Novas()[0].healerReachMs, 300);
    EXPECT_TRUE(watch.Freezes()[0].onHealer);
    EXPECT_EQ(watch.Freezes()[0].victim, Healer);
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

// The damage of the nova itself shows in the update that starts the freeze:
// it is no party damage.
TEST(PeelWatch, TheDamageOfTheNovaItselfBreaksNoFreeze)
{
    PeelWatch watch;
    watch.Update(Seen({On(Enforcer, Healer, 2.0f)}), 0);
    watch.Update(Seen({Frozen(On(Enforcer, Healer, 2.0f, 980))}), 500);
    watch.Update(Seen({Frozen(On(Enforcer, Healer, 2.0f, 980))}), 1500);
    watch.Update(Seen({On(Enforcer, Healer, 2.0f, 980)}), 2000);

    ASSERT_EQ(watch.Freezes().size(), 1u);
    EXPECT_FALSE(watch.Freezes()[0].brokenByParty);
    EXPECT_EQ(watch.NovasBrokenByParty(), 0u);
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

// Final review of spec #142, finding 14 and the fix-up runs. Spec #142: "The
// tank goes to the frozen enemies and picks them up. Breaking the freeze is
// fine."
namespace
{
    // A Frost Nova of `caster` went out with `inRadius` in its 10 yd.
    PeelSeen NovaOut(PeelSeen seen, std::vector<Bots::Guid> inRadius, Bots::Guid caster = Mage)
    {
        Bots::PeelNovaOut out;
        out.caster = caster;
        out.enemiesInRadius = inRadius;
        seen.novasOut.push_back(out);
        return seen;
    }
}

// Seen in the fix-up run 2 (133500): the tank came 0.1 s after the nova,
// took the frozen enemy and broke the freeze 0.3 s later, before the healer
// could step back.
TEST(PeelWatch, ATankThatPicksUpTheFrozenEnemyNeedsNoStepBack)
{
    PeelWatch watch;
    watch.Update(Seen({On(Enforcer, Healer, 2.0f)}), 0);
    watch.Update(Seen({HealerAt(Frozen(On(Enforcer, Healer, 2.0f)), 4.6f)}), 1000);
    watch.Update(Seen({TankHas(HealerAt(Frozen(On(Enforcer, Tank, 1.0f)), 4.6f))}, 1.4f), 1100);
    watch.Update(Seen({HealerAt(On(Enforcer, Mage, 2.0f, 900), 4.6f)}, 1.4f), 1300);

    ASSERT_EQ(watch.Freezes().size(), 1u);
    EXPECT_EQ(watch.Freezes()[0].tankHoldsMs, 1100);
    EXPECT_FALSE(watch.Freezes()[0].brokenByParty);
    EXPECT_EQ(Bots::PeelFailure(watch), "");
}

// The tank in melee of the frozen enemy is no pickup while the enemy still
// attacks the healer: the healer must step back.
TEST(PeelWatch, ATankThatOnlyHitsTheFrozenEnemyStillNeedsTheStepBack)
{
    PeelWatch watch;
    watch.Update(Seen({On(Enforcer, Healer, 2.0f)}), 0);
    watch.Update(Seen({HealerAt(Frozen(On(Enforcer, Healer, 2.0f)), 4.6f)}), 1000);
    watch.Update(Seen({TankHas(HealerAt(Frozen(On(Enforcer, Healer, 2.0f)), 4.6f))}), 1100);
    watch.Update(Seen({TankHas(HealerAt(On(Enforcer, Healer, 2.0f, 900), 4.6f))}), 1300);

    EXPECT_EQ(watch.Freezes()[0].tankHoldsMs, -1);
    EXPECT_EQ(Bots::PeelFailure(watch), "the healer ended 4.6 yd from the frozen enemy (at least 8 yd)");
}

// Seen in the fix-up run 1 (133359): the nova went out with the enemy in its
// radius and froze nothing (a resist).
TEST(PeelWatch, ANovaThatFreezesNothingIsReportedAndDoesNotFailAlone)
{
    PeelWatch watch;
    watch.Update(Seen({On(Enforcer, Healer, 2.0f)}), 0);
    watch.Update(NovaOut(Seen({On(Enforcer, Healer, 2.0f)}), {Enforcer}), 300);
    watch.Update(Seen({On(Enforcer, Healer, 2.0f)}), 1500);
    watch.Update(Seen({TankHas(On(Enforcer, Tank, 1.0f))}), 2500);

    std::vector<Bots::PeelCast> const nothing = watch.NovasThatFrozeNothing();
    ASSERT_EQ(nothing.size(), 1u);
    EXPECT_EQ(nothing[0].caster, Mage);
    EXPECT_EQ(nothing[0].ms, 300);
    EXPECT_EQ(nothing[0].enemiesInRadius, 1u);
    EXPECT_EQ(nothing[0].healerReachMs, 300);
    EXPECT_TRUE(watch.Novas().empty());
    EXPECT_EQ(Bots::PeelFailure(watch), "");
}

TEST(PeelWatch, ANovaThatFreezesInTheSameUpdateFrozeSomething)
{
    PeelWatch watch;
    watch.Update(Seen({On(Enforcer, Healer, 2.0f)}), 0);
    watch.Update(NovaOut(Seen({Frozen(On(Enforcer, Healer, 2.0f))}), {Enforcer}), 500);

    EXPECT_TRUE(watch.NovasThatFrozeNothing().empty());
    EXPECT_EQ(watch.Novas().size(), 1u);
}

TEST(PeelWatch, AfterANovaThatFreezesNothingTheEnemyOnTheHealerPastTheThreatLossLimitFails)
{
    PeelWatch watch;
    watch.Update(Seen({On(Enforcer, Healer, 2.0f)}), 0);
    watch.Update(NovaOut(Seen({On(Enforcer, Healer, 2.0f)}), {Enforcer}), 300);
    watch.Update(Seen({On(Enforcer, Healer, 2.0f)}), 4500);
    watch.Update(Seen({TankHas(On(Enforcer, Tank, 1.0f))}), 5000);

    EXPECT_EQ(Bots::PeelFailure(watch),
        "a Frost Nova froze nothing, and the enemy stayed on the healer for 4.5 s (limit 4 s)");
}

TEST(PeelWatch, ANovaThatFreezesNothingWithoutTheHealersEnemyInRadiusFails)
{
    PeelWatch watch;
    watch.Update(Seen({On(Enforcer, Healer, 2.0f)}), 0);
    watch.Update(NovaOut(Seen({On(Enforcer, Healer, 2.0f)}), {}), 300);
    watch.Update(Seen({TankHas(On(Enforcer, Tank, 1.0f))}), 2500);

    EXPECT_TRUE(watch.NovasThatFrozeNothing().empty());
    EXPECT_EQ(Bots::PeelFailure(watch), "no Frost Nova froze an enemy on the healer");
}

TEST(PeelWatch, ALateNovaThatFreezesNothingFails)
{
    PeelWatch watch;
    watch.Update(Seen({On(Enforcer, Healer, 2.0f)}), 0);
    watch.Update(NovaOut(Seen({On(Enforcer, Healer, 2.0f)}), {Enforcer}), 2600);
    watch.Update(Seen({TankHas(On(Enforcer, Tank, 1.0f))}), 3000);

    EXPECT_EQ(Bots::PeelFailure(watch), "the nova came 2.6 s after the enemy reached the healer (limit 2.0 s)");
}
