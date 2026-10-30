"""The changed stock Blacksmithing recipes of jvbastel/WOWAI#228 (spec #225) in the generated data.

The `stock` rows of forever/spec/blacksmithing.tsv change stock recipes in place: their Forever colour range
(client SkillLineAbility.dbc and forever_skill_line_ability), the skill that their trainer and their plans
need (forever_npc_trainer, forever_item_template) and, for 10 of them, their reagents or their name (client
Spell.dbc and forever_spell_template). The test runs the generator on the spec folder and reads what it
wrote from the outside. The worked examples come from the Forever client dumps of #224
(notes/forever/professions-mining-blacksmithing-research.md, section 3.2).
"""
import re
from collections import Counter

import pytest

from test_blacksmithing import BLACKSMITHING, counted, spec_rows, sql_rows
from test_pipeline import STOCK, generate, read_dbc

pytestmark = pytest.mark.skipif(not (STOCK / "Spell.dbc").is_file(), reason="no stock DBC folder at %s" % STOCK)

# Spell.dbc 1.12.1: 15 RequiresSpellFocus, 42-49 Reagent, 50-57 ReagentCount, 120 Name (enUS).
FOCUS, REAGENTS, REAGENT_COUNTS, NAME = 15, slice(42, 50), slice(50, 58), 120
ANVIL = 1
# SkillLineAbility.dbc 1.12.1: 1 SkillLine, 2 Spell, 10 grey, 11 yellow.
SLA_SPELL, SLA_GREY, SLA_YELLOW = 2, 10, 11

# The 22 stock recipes that need a Master Forge in Forever (research section 3.2). They keep the Anvil.
MASTER_FORGE = {"Lionheart Helm", "Arcanite Reaper", "Hammer of the Titans", "Arcanite Champion",
                "Masterwork Stormhammer", "Frostguard", "Heartseeker", "Blood Talon", "Darkspear", "Whitesoul Helm",
                "Stronghold Gauntlets", "Enchanted Thorium Helm", "Enchanted Thorium Leggings",
                "Enchanted Thorium Breastplate", "Gloves of the Dawn", "Heavy Obsidian Belt", "Titanic Leggings",
                "Persuader", "Sageblade", "Ironvine Breastplate", "Ironvine Gloves", "Ironvine Belt"}
# The 10 reagent and name changes (research section 3.2): Forever reagents and names.
FOREVER_REAGENTS = {3115: [(2835, 1)], 3116: [(2836, 1)], 3117: [(2838, 1)], 9921: [(7912, 1)], 16640: [(12365, 1)],
                    3324: [(2840, 8), (3470, 3), (2320, 3)]}
FOREVER_NAMES = {9931: "Mithril Plate Pants", 9937: "Mithril Plate Bracers", 9942: "Mithril Plate Gloves",
                 9966: "Mithril Plate Shoulders"}


def stock_rows():
    return [row for row in spec_rows("blacksmithing.tsv") if row["kind"] == "stock"]


def stock_abilities():
    """{spell: SkillLineAbility.dbc row} of the stock Blacksmithing recipes."""
    abilities, _ = read_dbc(STOCK / "SkillLineAbility.dbc")
    return {row[SLA_SPELL]: row for row in abilities.values() if row[1] == BLACKSMITHING}


@pytest.fixture(scope="module")
def out(tmp_path_factory):
    return generate(tmp_path_factory.mktemp("forever228"))


def test_the_spec_shifts_185_stock_colour_ranges():
    # The ticket says 175; the dump of #224 has 185 (169 by 25, 6 by 5, 3 by 30, 3 by 20, 2 by 15, 2 by 10).
    stock = stock_abilities()
    shifts = Counter()
    for row in stock_rows():
        old = stock[int(row["spell"])]
        down = (old[SLA_YELLOW] - int(row["yellow"]), old[SLA_GREY] - int(row["grey"]))
        assert down[0] == down[1], row["name"]
        if down[0]:
            shifts[down[0]] += 1
    assert shifts == {25: 169, 5: 6, 30: 3, 20: 3, 15: 2, 10: 2}


def test_the_worked_examples_of_the_colour_and_plans_shift():
    by_spell = {int(row["spell"]): row for row in stock_rows()}
    # Runed Copper Breastplate 120-160 -> 95-135, its plans 2881 80 -> 55. Silver Rod 105-110 -> 80-85.
    # Copper Chain Belt 75-115 -> 70-110. Frost Tiger Blade: plans 3868 200 -> 175.
    assert (by_spell[2667]["yellow"], by_spell[2667]["grey"], by_spell[2667]["plans"], by_spell[2667]["minRank"]) == \
        ("95", "135", "2881", "55")
    assert (by_spell[7818]["yellow"], by_spell[7818]["grey"]) == ("80", "85")
    assert (by_spell[2661]["yellow"], by_spell[2661]["grey"]) == ("70", "110")
    assert (by_spell[3497]["plans"], by_spell[3497]["minRank"]) == ("3868", "175")


def test_the_spec_has_the_10_reagent_and_name_changes():
    by_spell = {int(row["spell"]): row for row in stock_rows()}
    for spell, reagents in FOREVER_REAGENTS.items():
        assert counted(by_spell[spell]["reagents"]) == reagents, spell
    for spell, name in FOREVER_NAMES.items():
        assert by_spell[spell]["name"] == name


def test_the_22_master_forge_recipes_keep_the_anvil_in_the_spec():
    kept = [row for row in stock_rows() if "Master Forge" in row["notes"]]
    assert {row["name"] for row in kept} == MASTER_FORGE
    assert all(row["focus"] == "Anvil" for row in kept)


def test_each_stock_recipe_has_its_forever_colour_range_in_the_client_in_place_of_the_stock_one(out):
    client, _ = read_dbc(out / "client" / "DBFilesClient" / "SkillLineAbility.dbc")
    stock, _ = read_dbc(STOCK / "SkillLineAbility.dbc")
    rows = {int(row["spell"]): row for row in stock_rows()}
    assert set(client) >= set(stock)
    for key, old in stock.items():
        new = client[key]
        spec = rows.get(old[SLA_SPELL]) if old[1] == BLACKSMITHING else None
        if spec is None:
            assert new == old, key
            continue
        assert (new[SLA_YELLOW], new[SLA_GREY]) == (int(spec["yellow"]), int(spec["grey"])), spec["name"]
        assert new[:SLA_GREY] + new[SLA_YELLOW + 1:] == old[:SLA_GREY] + old[SLA_YELLOW + 1:], spec["name"]
    assert not [row for key, row in client.items() if key not in stock and row[SLA_SPELL] in rows]
    # Runed Copper Breastplate (SLA 1647 in 1.12): 120-160 -> 95-135.
    assert (client[stock_abilities()[2667][0]][SLA_YELLOW], client[stock_abilities()[2667][0]][SLA_GREY]) == (95, 135)
