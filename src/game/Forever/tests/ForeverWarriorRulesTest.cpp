// Tests of the pure math of the Forever Warrior mechanics (ForeverWarriorRules.h, jvbastel/WOWAI#214).
// No GoogleTest and no core: wowai/tests/forever-rules.test.sh builds this file with the host
// compiler and runs it. The glob of src/game/Forever does not take this subfolder into the core.

#include "../ForeverWarriorRules.h"
#include "TestCheck.h"

#include <cmath>
#include <cstdio>


int main()
{
    using namespace ForeverTest;

    using namespace Forever::WarriorRules;

    // Rage from weapon speed.
    Near("a 1.9 sec one-hand hit gives 1.9 x 3.46 rage", SwingRage(1900, false, false, false), 6.574f);
    Near("a 2.9 sec two-hand hit gives 2.9 x 4.5 rage", SwingRage(2900, true, false, false), 13.05f);
    Near("a 2.2 sec sword gives 7.6 rage (kami-labs: 7 per hit)", SwingRage(2200, false, false, false), 7.612f);
    Near("a slow weapon gives more rage per hit than a fast one",
         SwingRage(3800, true, false, false) - SwingRage(1500, false, false, false), 17.1f - 5.19f);
    Near("unarmed swings at the core's unarmed speed", SwingRage(UnarmedSpeedMs, false, false, false), 6.92f);
    Near("an off-hand hit gives half", SwingRage(1900, false, true, false), 3.287f);
    Near("a crit gives 100% more", SwingRage(2900, true, false, true), 26.1f);
    Near("an off-hand crit: half, then double", SwingRage(1900, false, true, true), 6.574f);

    // DoT crits.
    Near("a DoT tick crit deals double", DotCritDamage(5.0f, 0.0f), 10.0f);
    Near("Impale 2/2 adds 20% of the normal tick", DotCritDamage(5.0f, 20.0f), 11.0f);

    // Deep Wounds.
    Near("Deep Wounds 3/3: 60% of the weapon average", DeepWoundsDamage(10.0f, 20.0f, 60.0f), 9.0f);
    Near("a first bleed spreads its damage over 4 ticks", DeepWoundsRolledTick(12.0f, 0.0f, 0), 3.0f);
    Near("a new bleed rolls the 2 ticks the old one had left", DeepWoundsRolledTick(12.0f, 2.0f, 2), 4.0f);
    Near("an old bleed that ticked out adds nothing", DeepWoundsRolledTick(12.0f, 2.0f, 0), 3.0f);
    Near("at most the full duration of the old bleed rolls", DeepWoundsRolledTick(12.0f, 2.0f, 9), 5.0f);
    Near("a negative old tick adds nothing", DeepWoundsRolledTick(12.0f, -3.0f, 2), 3.0f);

    return ForeverTest::Summary();
}
