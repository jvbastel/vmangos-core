// Tests of the spell IDs of the Forever rules (ForeverSpellIds.h, spec jvbastel/WOWAI#204).
// forever/generator/tests/test_spell_ids.py checks the ID values against forever/warrior.tsv; this test
// checks the rules that use them.

#include "../ForeverSpellIds.h"
#include "TestCheck.h"

int main()
{
    using namespace ForeverTest;
    using namespace Forever::SpellIds;

    // Review S1: with the rules off, a Forever-only spell does not exist, and the stock load deletes
    // the character_spell rows of an unknown spell for all characters. The rows of the free range stay.
    Check("rules off: a Forever-only talent spell keeps its rows", KeepsRowsOfUnknownSpell(60119, false));
    Check("rules off: the first ID of the free range keeps its rows", KeepsRowsOfUnknownSpell(FreeIdFirst, false));
    Check("rules off: the last ID of the free range keeps its rows", KeepsRowsOfUnknownSpell(FreeIdLast, false));
    Check("rules off: an unknown stock ID below the range is cleaned up as in stock", !KeepsRowsOfUnknownSpell(FreeIdFirst - 1, false));
    Check("rules off: an ID above the range is cleaned up as in stock", !KeepsRowsOfUnknownSpell(FreeIdLast + 1, false));
    Check("rules on: an unknown Forever ID (dropped from the spec) is cleaned up as in stock", !KeepsRowsOfUnknownSpell(60119, true));

    Check("the free range starts above the stock 1.12.1 spells", FreeIdFirst > 33392);
    Check("a new talent rank is a Forever-only spell", IsForeverOnlySpell(VanguardTalent));
    Check("a kept stock talent rank is no Forever-only spell", !IsForeverOnlySpell(WeaponmasterRanks[0]));

    return Summary();
}
