"""US35 of spec jvbastel/WOWAI#225: the generator refuses a row with a missing source, for every row kind.

The new recipe and item rows have their own refusals (test_new_recipes.py). Here: a changed stock recipe needs
the Forever spell it comes from, a removal row the reason (its notes), an item set its Forever set (notes) and
each set bonus its Forever spell, and a mining drop the source of its rate (already its `rate` column).
"""
import pytest

from test_blacksmithing import refused
from test_pipeline import STOCK

pytestmark = pytest.mark.skipif(not (STOCK / "SkillLineAbility.dbc").is_file(), reason="no stock DBC folder at %s" % STOCK)

BELT = "Anvil\t-\t2661\tColours 75-115"
SET_NOTES = ("New set. Forever ItemSet 1968; pieces in its order: Forever items 250594, 250593, 250592, 250591, "
             "250590, 250588, 250587, 250586 (the #230 items).")
REMOVED = "\t19202\t-\tNot in the Forever client. Stock SkillLineAbility 12677, learn spell 23630, plans 19202."


def test_a_stock_recipe_row_with_no_forever_spell_is_refused(tmp_path):
    error = refused(tmp_path, "blacksmithing.tsv", (BELT, BELT.replace("\t2661\t", "\t-\t")))
    assert "stock recipe Copper Chain Belt: spell 2661 has no source (foreverSpell)" in error


def test_a_removal_row_with_no_notes_is_refused(tmp_path):
    error = refused(tmp_path, "blacksmithing.tsv", (REMOVED, "\t19202\t-\t-"))
    assert "removed recipe Heavy Timbermaw Belt: spell 23628 has no source (notes: why Forever removed it)" in error


def test_an_item_set_with_no_notes_is_refused(tmp_path):
    error = refused(tmp_path, "item_sets.tsv", (SET_NOTES, "-"))
    assert "item set Blessed Plate (1968) has no source (notes: its Forever item set)" in error


def test_a_set_bonus_with_no_forever_spell_is_refused(tmp_path):
    error = refused(tmp_path, "item_sets.tsv", ("ShadowRes 10\t14673\t", "ShadowRes 10\t-\t"))
    assert "Blessed Plate (2)" in error and "has no source (foreverSpell)" in error
