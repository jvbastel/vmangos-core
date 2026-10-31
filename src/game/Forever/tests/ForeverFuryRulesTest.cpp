// Tests of the pure rules of the Forever Fury talents (ForeverFuryRules.h, jvbastel/WOWAI#218).
// No GoogleTest and no core: wowai/tests/forever-rules.test.sh builds this file with the host
// compiler and runs it. The glob of src/game/Forever does not take this subfolder into the core.
// Expected values come from the spec rows of forever/spec/warrior.tsv and the tooltips of build 1.60.1.70685.

#include "../ForeverFuryRules.h"
#include "TestCheck.h"

#include <cstdio>


int main()
{
    using namespace ForeverTest;

    using namespace Forever::FuryRules;

    // Furious Precision: off-hand attacks only.
    Equal("Furious Precision 3/3 gives an off-hand attack +10% hit", OffHandHitBonus(true, 10), 10);
    Equal("Furious Precision gives a main hand attack nothing", OffHandHitBonus(false, 10), 0);
    Equal("no Furious Precision, no bonus", OffHandHitBonus(true, 0), 0);

    // Blood Craze: a single hit of more than 20% of maximum health.
    Check("a hit of 21% of maximum health triggers Blood Craze", BloodCrazeTriggers(210, 1000, 20));
    Check("a hit of exactly 20% does not (more than 20%)", !BloodCrazeTriggers(200, 1000, 20));
    Check("a hit of 19% does not", !BloodCrazeTriggers(190, 1000, 20));
    Check("a big hit on a big health pool does not overflow", BloodCrazeTriggers(900000000u, 1000000000u, 20));
    Check("no maximum health, no trigger", !BloodCrazeTriggers(5, 0, 20));

    // Gore Drinker: 0.5% (rank 1, 5 tenths) and 1% (rank 2, 10 tenths) of maximum health per melee attack.
    Equal("Gore Drinker 1/2 heals 0.5% of 4000 health", GoreDrinkerHeal(4000, 5), 20);
    Equal("Gore Drinker 2/2 heals 1% of 4000 health", GoreDrinkerHeal(4000, 10), 40);
    Equal("a tiny health pool still heals 1", GoreDrinkerHeal(100, 5), 1);
    Equal("no talent value heals nothing", GoreDrinkerHeal(4000, 0), 0);

    // Gore Drinker triggers: Enrage, Berserker Rage, Bloodrage, Death Wish and Bloodthirst.
    Check("the Enrage buff of Enrage 1/5 (12880) triggers", IsGoreDrinkerTrigger(12880));
    Check("the Enrage buff of Enrage 5/5 (14204) triggers", IsGoreDrinkerTrigger(14204));
    Check("Berserker Rage (18499) triggers", IsGoreDrinkerTrigger(18499));
    Check("Bloodrage (2687) triggers", IsGoreDrinkerTrigger(2687));
    Check("Death Wish (12328) triggers", IsGoreDrinkerTrigger(12328));
    Check("Bloodthirst, the talent rank (23881), triggers", IsGoreDrinkerTrigger(23881));
    Check("Bloodthirst rank 4 (23894) triggers", IsGoreDrinkerTrigger(23894));
    Check("the Rage over time of Bloodrage (29131) does not trigger a second time", !IsGoreDrinkerTrigger(29131));
    Check("Recklessness (1719) does not trigger", !IsGoreDrinkerTrigger(1719));
    Check("Battle Shout (6673) does not trigger", !IsGoreDrinkerTrigger(6673));

    // Improved Berserker Rage: the snare removal roll (50% at rank 1, 100% at rank 2).
    Check("a roll of 49.9 under 50% removes the snares", RemovesSnares(50, 49.9f));
    Check("a roll of 50.0 under 50% does not", !RemovesSnares(50, 50.0f));
    Check("rank 2 (100%) always removes them", RemovesSnares(100, 99.99f));
    Check("no talent never removes them", !RemovesSnares(0, 0.0f));

    // Lingering Rage: the rage does not decay while the talent's delay runs after combat.
    Check("in combat, rage does not decay", !RageDecays(true, false));
    Check("out of combat, rage decays", RageDecays(false, false));
    Check("out of combat with the Lingering Rage delay running, rage does not decay", !RageDecays(false, true));

    // The talent rank that a Warrior has: the highest rank spell that it knows.
    uint32_t const known[] = { 60107 };
    Equal("Lingering Rage 2/5 is the second rank", int(KnownRank(LingeringRageRanks, [&](uint32_t id) { return id == known[0]; })), 2);
    Equal("no rank known gives 0", int(KnownRank(GoreDrinkerRanks, [](uint32_t) { return false; })), 0);
    Equal("all ranks known gives the highest", int(KnownRank(FuriousPrecisionRanks, [](uint32_t) { return true; })), 3);

    return ForeverTest::Summary();
}
