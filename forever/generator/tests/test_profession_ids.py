"""The profession ID ranges that the rules switch knows by number (src/game/Forever/ForeverProfessionIds.h,
jvbastel/WOWAI#233) against the free range lines of the spec files. With the rules off the core tells a Forever
recipe or item apart only by its ID, so a spec range that moves must fail here, not in the game."""
import re

from forever_gen import items, spec
from test_pipeline import ROOT, SPEC_DIR

HEADER = ROOT / "src" / "game" / "Forever" / "ForeverProfessionIds.h"


def constant(name):
    m = re.search(r"constexpr uint32_t %s = (\d+);" % name, HEADER.read_text(encoding="utf-8"))
    assert m, "%s is not in %s" % (name, HEADER.name)
    return int(m.group(1))


def test_the_profession_spell_block_is_the_one_of_the_spec():
    the_spec = spec.read(SPEC_DIR)
    assert (constant("ProfessionSpellFirst"), constant("ProfessionSpellLast")) == the_spec.profession_id_range


def test_the_forever_item_range_is_the_one_of_the_spec():
    the_spec = spec.read(SPEC_DIR)
    assert (constant("ItemIdFirst"), constant("ItemIdLast")) == the_spec.item_id_range


def test_every_spec_recipe_and_item_is_in_the_ranges_the_core_keeps():
    the_spec = spec.read(SPEC_DIR)
    low, high = constant("ProfessionSpellFirst"), constant("ProfessionSpellLast")
    assert the_spec.recipes, "no recipe rows read"
    for recipe in the_spec.recipes:
        assert low <= recipe.spell <= high, recipe
    first, last = constant("ItemIdFirst"), constant("ItemIdLast")
    new_items = [item for item in the_spec.items if item.kind == items.NEW]
    assert new_items, "no new item rows read"
    for item in new_items:
        assert first <= item.entry <= last, item
