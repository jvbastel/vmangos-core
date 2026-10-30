// Tests of the profession ID rules of the rules switch (ForeverProfessionIds.h, spec jvbastel/WOWAI#225, #233).
// forever/generator/tests/test_profession_ids.py checks the ID values against the spec files; this test checks
// the rules that use them.

#include "../ForeverProfessionIds.h"
#include "../ForeverSpellIds.h"
#include "TestCheck.h"

int main()
{
    using namespace ForeverTest;
    using namespace Forever::ProfessionIds;

    // The profession block is inside the free spell range, so the stock cleanup keeps its rows with the rules off.
    Check("the profession block is inside the free spell range",
        Forever::SpellIds::IsForeverOnlySpell(ProfessionSpellFirst) && Forever::SpellIds::IsForeverOnlySpell(ProfessionSpellLast));
    Check("rules off: a learned Forever recipe keeps its rows", Forever::SpellIds::KeepsRowsOfUnknownSpell(61000, false));

    // The Warrior switch-off delete: the free-range spells of the Warrior go, the recipes stay.
    Check("switch off: the Strange Copper Boots recipe stays", !WarriorSwitchOffDeletes(61000));
    Check("switch off: the first profession spell stays", !WarriorSwitchOffDeletes(ProfessionSpellFirst));
    Check("switch off: the last profession spell stays", !WarriorSwitchOffDeletes(ProfessionSpellLast));
    Check("switch off: the Forever talent Vanguard goes", WarriorSwitchOffDeletes(60119));
    Check("switch off: the Forever ability Victory Rush goes", WarriorSwitchOffDeletes(60123));
    Check("switch off: a free-range spell above the profession block goes", WarriorSwitchOffDeletes(ProfessionSpellLast + 1));
    Check("switch off: a free-range spell below the profession block goes", WarriorSwitchOffDeletes(ProfessionSpellFirst - 1));
    Check("switch off: a stock spell is not touched", !WarriorSwitchOffDeletes(12294));

    // Items: with the rules off, a Forever-only item does not exist; its inventory rows stay.
    Check("rules off: the Strange Copper Boots keep their rows", KeepsRowsOfUnknownItem(70000, false));
    Check("rules off: the last Forever item ID keeps its rows", KeepsRowsOfUnknownItem(ItemIdLast, false));
    Check("rules off: an unknown item below the range is deleted as in stock", !KeepsRowsOfUnknownItem(ItemIdFirst - 1, false));
    Check("rules off: an unknown item above the range is deleted as in stock", !KeepsRowsOfUnknownItem(ItemIdLast + 1, false));
    Check("rules on: an unknown Forever item (dropped from the spec) is deleted as in stock", !KeepsRowsOfUnknownItem(70000, true));
    Check("the item range starts above the stock 1.12 items", ItemIdFirst > 25818);

    // A removed stock recipe: hidden at load with the rules on, loaded as usual with the rules off.
    Check("rules on: a removed recipe is not loaded", HidesKnownSpell(true, true));
    Check("rules off: a removed recipe is loaded", !HidesKnownSpell(true, false));
    Check("rules on: another spell is loaded", !HidesKnownSpell(false, true));

    return Summary();
}
