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
from test_pipeline import STOCK, read_dbc, removed_stock_abilities

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
    gone = removed_stock_abilities()
    assert set(client) >= set(stock) - gone
    for key, old in stock.items():
        if key in gone:
            continue
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


def learn_spells(spell):
    """The stock spells that teach `spell` (Effect1 36 Learn Spell, EffectTriggerSpell1 = spell): the
    spells of the trainer rows."""
    spells, _ = read_dbc(STOCK / "Spell.dbc")
    return sorted(key for key, row in spells.items() if row[61] == 36 and row[109] == spell)


def sql(out):
    return (out / "sql" / "forever_world.sql").read_text(encoding="utf-8")


def test_each_stock_recipe_has_its_colour_range_in_the_server_skill_line_table_by_its_stock_id(out):
    rows = {int(row["id"]): row for row in sql_rows(out, "forever_sla_stock")}
    for spec in stock_rows():
        for ability in [row for row in read_dbc(STOCK / "SkillLineAbility.dbc")[0].values()
                        if row[SLA_SPELL] == int(spec["spell"]) and row[1] == BLACKSMITHING]:
            assert (rows[ability[0]]["min_value"], rows[ability[0]]["max_value"]) == (spec["yellow"], spec["grey"])
    assert len(rows) == len(stock_rows()) + 1  # Radiant Circlet has two stock rows


def test_the_trainer_rows_of_each_stock_recipe_need_its_forever_skill(out):
    text = sql(out)
    assert "DROP TABLE IF EXISTS `forever_npc_trainer`;\nCREATE TABLE `forever_npc_trainer` LIKE `npc_trainer`;" in text
    copies = dict(re.findall(r"^INSERT INTO `forever_npc_trainer` SELECT \* FROM `npc_trainer` WHERE `spell` IN \(([\d, ]+)\);\n"
                             r"UPDATE `forever_npc_trainer` SET `reqskillvalue`=(\d+) WHERE `spell` IN \(\1\);$", text, re.M))
    for spec in stock_rows():
        teaching = learn_spells(int(spec["spell"]))
        if teaching:
            assert copies[", ".join(map(str, teaching))] == spec["minRank"], spec["name"]
    # Runed Copper Bracers (2664): the trainers teach 2745 at 90 in 1.12, at 65 (the colour shift 25) in Forever.
    assert copies["2745"] == "65"


def test_a_trainer_copy_fails_the_load_when_a_trainer_template_teaches_the_spell(out):
    # Review L5: the core reads npc_trainer_template with no Forever override (ObjectMgr::LoadTrainers), so a
    # learn spell with template rows would keep its stock skill there. The SQL fails instead.
    text = sql(out)
    copies = re.findall(r"^(.*)\nINSERT INTO `forever_npc_trainer` SELECT \* FROM `npc_trainer` WHERE `spell` IN \(([\d, ]+)\);$",
                        text, re.M)
    assert copies
    for guard, teaching in copies:
        assert guard == ("SELECT IF((SELECT COUNT(*) FROM `npc_trainer_template` WHERE `spell` IN (%s))=0, 0, "
                         "(SELECT 1 UNION SELECT 2)) INTO @forever_found;" % teaching)


def test_the_plans_of_each_stock_recipe_need_its_forever_skill(out):
    plans = {int(row["entry"]): row for row in sql_rows(out, "forever_plans_new")}
    with_plans = [row for row in stock_rows() if row["plans"] != "-"]
    assert len(plans) == len(with_plans)
    for spec in with_plans:
        assert (plans[int(spec["plans"])]["required_skill_rank"], plans[int(spec["plans"])]["patch"]) == (spec["minRank"], "0")
    # Plans: Runed Copper Breastplate 80 -> 55, Plans: Frost Tiger Blade 200 -> 175.
    assert (plans[2881]["required_skill_rank"], plans[3868]["required_skill_rank"]) == ("55", "175")
    assert sql(out).count("INSERT INTO `forever_item_template` SELECT * FROM `forever_plans_new`;") == len(with_plans)


def test_the_plans_of_a_renamed_stock_recipe_carry_its_forever_name(out):
    # The Forever client: 7991 "Plans: Mithril Plate Shoulders", 7995 "Plans: Mithril Plate Bracers". 7977 is not
    # readable there and is assumed to follow. The other plans keep their stock name.
    plans = {int(row["entry"]): row for row in sql_rows(out, "forever_plans_new")}
    assert {entry: row["name"] for entry, row in plans.items() if "name" in row} == {
        7991: "Plans: Mithril Plate Shoulders", 7995: "Plans: Mithril Plate Bracers", 7977: "Plans: Mithril Plate Gloves"}


def test_the_reagent_and_name_changes_are_in_the_client_and_server_spells(out):
    client, text = read_dbc(out / "client" / "DBFilesClient" / "Spell.dbc")
    server = {int(row["entry"]): row for row in sql_rows(out, "forever_spell_new")}
    for spell, reagents in FOREVER_REAGENTS.items():
        padded = reagents + [(0, 0)] * (8 - len(reagents))
        assert list(zip(client[spell][REAGENTS], client[spell][REAGENT_COUNTS])) == padded, spell
        assert [(int(server[spell]["reagent%d" % k]), int(server[spell]["reagentCount%d" % k]))
                for k in range(1, 9)] == padded, spell
    for spell, name in FOREVER_NAMES.items():
        assert text(client[spell][NAME]) == name and server[spell]["name"] == name
    # Only those 10 stock recipes change their spell: the other stock rows change no Spell.dbc byte.
    stock, _ = read_dbc(STOCK / "Spell.dbc")
    changed = {int(row["spell"]) for row in stock_rows() if client[int(row["spell"])] != stock[int(row["spell"])]}
    assert changed == set(FOREVER_REAGENTS) | set(FOREVER_NAMES)
    assert {spell for spell in server if spell in stock} >= changed


def test_the_22_master_forge_recipes_keep_the_anvil_in_the_client_and_server(out):
    client, text = read_dbc(out / "client" / "DBFilesClient" / "Spell.dbc")
    server = {int(row["entry"]): row for row in sql_rows(out, "forever_spell_new")}
    by_name = {row["name"]: int(row["spell"]) for row in stock_rows()}
    for name in MASTER_FORGE:
        spell = by_name[name]
        assert client[spell][FOCUS] == ANVIL, name
        assert int(server.get(spell, {}).get("requiresSpellFocus", ANVIL)) == ANVIL, name


def refused(tmp_path, *changes):
    from test_blacksmithing import refused as refused_in
    return refused_in(tmp_path, "blacksmithing.tsv", *changes)


# Runed Copper Breastplate: its stock row and its plans.
BREASTPLATE = "stock\t2667\t2667\tRuned Copper Breastplate\t164\t55\t95\t135\t"


def test_a_stock_row_that_is_no_stock_spell_is_refused(tmp_path):
    error = refused(tmp_path, (BREASTPLATE, BREASTPLATE.replace("2667\t2667", "59990\t59990")))
    assert "spell 59990 is no stock spell" in error


def test_a_stock_row_with_another_source_spell_is_refused(tmp_path):
    error = refused(tmp_path, (BREASTPLATE, BREASTPLATE.replace("2667\t2667", "2667\t2666")))
    assert "changes the stock spell in place" in error


def test_a_stock_row_that_makes_another_item_is_refused(tmp_path):
    error = refused(tmp_path, ("\t2864x1\tAnvil\t2881\t", "\t2865x1\tAnvil\t2881\t"))
    assert "keeps the item that the stock spell makes" in error


def test_a_stock_row_with_plans_that_are_also_an_item_row_is_refused(tmp_path):
    error = refused(tmp_path, ("\t2864x1\tAnvil\t2881\t", "\t2864x1\tAnvil\t70000\t"))
    assert "plans 70000 are also an item row" in error


def test_a_warrior_spell_in_the_profession_spell_block_is_refused(tmp_path):
    # Follow-up of #226: the core tells a profession spell from a Warrior spell by the block 61000-61999.
    from test_blacksmithing import refused as refused_in
    from test_pipeline import SPEC
    first = next(line for line in SPEC.read_text(encoding="utf-8").splitlines() if line.startswith("ability\t"))
    spells = first.split("\t")[11]
    moved = first.replace("\t%s\t" % spells, "\t%s\t" % ",".join(str(61500 + k) for k in range(len(spells.split(",")))), 1)
    error = refused_in(tmp_path, "warrior.tsv", (first, moved))
    assert "is inside the profession spell ID range 61000-61999" in error


def test_plans_named_by_two_stock_rows_are_refused(tmp_path):
    # Plans: Runed Copper Breastplate (2881) also on Runed Copper Bracers (2664, a trainer recipe).
    error = refused(tmp_path, ("\t2854x1\tAnvil\t-\t2664\t", "\t2854x1\tAnvil\t2881\t2664\t"))
    assert "plans 2881 are named twice" in error
