// Tests of the pure rules of the Forever Arms talents (ForeverArmsRules.h, jvbastel/WOWAI#217).
// No GoogleTest and no core: wowai/tests/forever-rules.test.sh builds this file with the host
// compiler and runs it. The glob of src/game/Forever does not take this subfolder into the core.

#include "../ForeverArmsRules.h"
#include "TestCheck.h"

#include <cmath>
#include <cstdio>


int main()
{
    using namespace ForeverTest;

    using namespace Forever::ArmsRules;

    // Improved Tactical Mastery: the stance change keeps the baseline 10 Rage plus 3/6/9/12/15.
    Check("baseline Tactical Mastery keeps 10 Rage of 40", RageAfterStanceChange(400, 100, 0) == 100);
    Check("Improved Tactical Mastery 5/5 keeps 25 Rage of 40", RageAfterStanceChange(400, 100, 15) == 250);
    Check("rank 1 keeps 13 Rage", RageAfterStanceChange(400, 100, 3) == 130);
    Check("less Rage than the cap stays as it is", RageAfterStanceChange(120, 100, 15) == 120);
    Check("without baseline Tactical Mastery the talent alone counts", RageAfterStanceChange(400, 0, 6) == 60);

    // Spearing Strike: 40% weapon damage, plus 80% (x3 in all) against Giants, Dragonkin and mounted targets.
    Check("a Giant takes the bonus", IsSpearingBonusTarget(CreatureTypeGiant, false));
    Check("a Dragonkin takes the bonus", IsSpearingBonusTarget(CreatureTypeDragonkin, false));
    Check("a mounted Humanoid takes the bonus", IsSpearingBonusTarget(7, true));
    Check("a Beast on foot takes no bonus", !IsSpearingBonusTarget(1, false));
    Near("40% + 80% weapon damage is x3 of the 40%", SpearingStrikeMultiplier(true, 2), 3.0f);
    Near("no bonus target keeps the 40%", SpearingStrikeMultiplier(false, 2), 1.0f);

    // Bloodthrill: main hand swings and abilities that land on a target with the Warrior's own Rend.
    Check("a main hand swing that hits a Rend target counts", BloodthrillCounts(true, true, true));
    Check("an off-hand swing does not count", !BloodthrillCounts(false, true, true));
    Check("a miss does not count", !BloodthrillCounts(true, false, true));
    Check("no Rend of the Warrior, no proc", !BloodthrillCounts(true, true, false));
    Check("the Overpower window is 5 sec (client 1282733)", BloodthrillWindowMs == 5000);

    // Weaponmaster: the benefit depends on the weapon type of the attack.
    Check("a one-hand axe gives crit", WeaponmasterBenefitOf(0) == WeaponmasterBenefit::Crit);
    Check("a two-hand axe gives crit", WeaponmasterBenefitOf(1) == WeaponmasterBenefit::Crit);
    Check("a polearm gives crit", WeaponmasterBenefitOf(6) == WeaponmasterBenefit::Crit);
    Check("a one-hand mace ignores armor", WeaponmasterBenefitOf(4) == WeaponmasterBenefit::ArmorIgnore);
    Check("a two-hand mace ignores armor", WeaponmasterBenefitOf(5) == WeaponmasterBenefit::ArmorIgnore);
    Check("a staff ignores armor", WeaponmasterBenefitOf(10) == WeaponmasterBenefit::ArmorIgnore);
    Check("a one-hand sword gives the extra attack", WeaponmasterBenefitOf(7) == WeaponmasterBenefit::ExtraAttack);
    Check("a two-hand sword gives the extra attack", WeaponmasterBenefitOf(8) == WeaponmasterBenefit::ExtraAttack);
    Check("a dagger gives nothing", WeaponmasterBenefitOf(15) == WeaponmasterBenefit::None);
    Check("a fist weapon gives nothing", WeaponmasterBenefitOf(13) == WeaponmasterBenefit::None);
    Near("Weaponmaster 5/5 ignores 15% of 3000 armor", ArmorAfterIgnore(3000.0f, 15.0f), 2550.0f);
    Near("rank 1 ignores 3%", ArmorAfterIgnore(1000.0f, 3.0f), 970.0f);
    Near("no armor stays none", ArmorAfterIgnore(0.0f, 15.0f), 0.0f);

    // Improved Slam: only Slam swings through its cast, and only with the talent.
    Check("Slam with Improved Slam keeps the swing", SlamKeepsSwing(true, true));
    Check("Slam without the talent stops the swing (stock)", !SlamKeepsSwing(true, false));
    Check("another cast with the talent stops the swing (stock)", !SlamKeepsSwing(false, true));

    return ForeverTest::Summary();
}
