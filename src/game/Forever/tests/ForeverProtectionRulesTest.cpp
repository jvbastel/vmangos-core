// Tests of the pure rules of the Forever Protection talents (ForeverProtectionRules.h, jvbastel/WOWAI#219).
// No GoogleTest and no core: wowai/tests/forever-rules.test.sh builds this file with the host
// compiler and runs it. The glob of src/game/Forever does not take this subfolder into the core.

#include "../ForeverProtectionRules.h"
#include "TestCheck.h"

#include <cstdio>


int main()
{
    using namespace ForeverTest;

    using namespace Forever::ProtectionRules;

    // The talent ranks, from forever/spec/warrior.tsv.
    Check("each Defiance rank is shield gated", IsShieldTalentSpell(12303) && IsShieldTalentSpell(12788) &&
          IsShieldTalentSpell(12789));
    Check("each Bastion rank is shield gated", IsShieldTalentSpell(16538) && IsShieldTalentSpell(16539) &&
          IsShieldTalentSpell(16540) && IsShieldTalentSpell(16541) && IsShieldTalentSpell(16542));
    Check("the stock Defiance ranks 4-5 are not (Forever has 3 ranks)",
          !IsShieldTalentSpell(12791) && !IsShieldTalentSpell(12792));
    Check("Shield Specialization and Iron Will are not shield gated",
          !IsShieldTalentSpell(12298) && !IsShieldTalentSpell(12300));
    Check("Master of Defense is ranks 60117 and 60118",
          IsMasterOfDefenseSpell(60117) && IsMasterOfDefenseSpell(60118) && !IsMasterOfDefenseSpell(60119));
    Check("Iron Will is ranks 12300, 12959-12962", IsIronWillSpell(12300) && IsIronWillSpell(12959) &&
          IsIronWillSpell(12960) && IsIronWillSpell(12961) && IsIronWillSpell(12962) && !IsIronWillSpell(12963));
    Equal("Master of Defense gives the Rage of spell 23602", MasterOfDefenseRageSpell, 23602);

    // Defiance and Bastion: active only with a shield, and in the stance that the spell asks for.
    Check("a shield in the right stance: the passive is on", ShieldTalentActive(true, true));
    Check("no shield: the passive is off", !ShieldTalentActive(false, true));
    Check("a shield in another stance: the passive is off", !ShieldTalentActive(true, false));

    // Master of Defense.
    Check("a dodge or parry with a shield and the talent can give Rage", MasterOfDefenseCanProc(true, true, 50));
    Check("no shield: no Rage", !MasterOfDefenseCanProc(true, false, 100));
    Check("a hit or a block (not a dodge or parry): no Rage", !MasterOfDefenseCanProc(false, true, 100));
    Check("no talent (chance 0): no Rage", !MasterOfDefenseCanProc(true, true, 0));

    // Iron Will: Charm and Stun durations (client 70685: mechanics 1 and 12), 3/6/9/12/15% shorter.
    Check("Iron Will shortens a Stun", IronWillShortens(1u << (12 - 1)));
    Check("Iron Will shortens a Charm", IronWillShortens(1u << (1 - 1)));
    Check("Iron Will shortens a spell with a Stun among other mechanics", IronWillShortens((1u << (12 - 1)) | (1u << (7 - 1))));
    Check("Iron Will does not shorten a Fear", !IronWillShortens(1u << (5 - 1)));
    Check("Iron Will does not shorten a spell with no mechanic", !IronWillShortens(0));
    Equal("Iron Will 5/5 makes a 6 sec stun 5.1 sec", IronWillDuration(6000, 15), 5100);
    Equal("Iron Will 1/5 makes a 10 sec charm 9.7 sec", IronWillDuration(10000, 3), 9700);
    Equal("no Iron Will: the duration stays", IronWillDuration(3000, 0), 3000);
    Equal("a duration that does not divide evenly rounds down", IronWillDuration(1001, 15), 850);
    Equal("a value above 100% never makes a negative duration", IronWillDuration(3000, 150), 0);
    Equal("a negative value never makes an effect longer", IronWillDuration(3000, -20), 3000);
    Equal("an endless effect (-1) stays endless", IronWillDuration(-1, 15), -1);
    Equal("a long duration does not overflow", IronWillDuration(2000000000, 15), 1700000000);

    return ForeverTest::Summary();
}
