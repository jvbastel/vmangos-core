"""The new Forever Blacksmithing recipes and items of jvbastel/WOWAI#230 (spec #225) and the dead-content rule.

The rule tests run the generator on a copy of the spec folder with a small made-up family of rows added
(the Heavy Thorium chain in miniature), and read the cut list that it prints and writes. The refusal tests
change one thing and expect the generator to fail and write nothing. The real-spec tests read the generated
data of forever/spec from the outside and compare it with the rows, the Forever client dump (outside git,
FOREVER_CLIENT_DUMP) and the worked examples of the ticket.
"""
import csv
import os
import subprocess
import sys
from pathlib import Path

import pytest

from test_blacksmithing import counted, free_range, spec_rows, sql_rows
from test_pipeline import DUMP, GENERATOR, SPEC_DIR, STOCK, pytestmark, read_dbc  # noqa: F401  (pytestmark: skip without stock data)

# Spell.dbc 1.12.1: 15 RequiresSpellFocus, 61 Effect1, 109 EffectTriggerSpell1, 120 Name (enUS).
FOCUS, EFFECT1, TRIGGER1, NAME = 15, 61, 109, 120
LEARN_SPELL = 36

RECIPE_COLUMNS = ("kind", "spell", "sourceSpell", "name", "skillLine", "minRank", "yellow", "grey", "reagents",
                  "creates", "focus", "plans", "foreverSpell", "notes")
ITEM_COLUMNS = ("kind", "entry", "sourceItem", "name", "quality", "itemLevel", "requiredLevel", "bonding",
                "inventoryType", "delay", "damage", "armor", "resistances", "stats", "bonuses", "equipSpells",
                "displayId", "sellPrice", "foreverItem", "foreverStats", "notes")


def recipe(spell, name, reagents, creates, focus="Anvil", plans="-", source=3319, skill=164, forever=None):
    return "\t".join([
        "recipe", str(spell), str(source), name, str(skill), "100", "100", "120", reagents, creates, focus, plans,
        str(forever or 1300000 + spell), "test row"])


def item(entry, name, source=3469, forever=None):
    return "\t".join([
        "item", str(entry), str(source), name, "uncommon", "20", "0", "none", "-", "-", "-", "0", "-", "-", "-", "-",
        "23528", "100", str(forever or 300000 + entry), "-", "test row"])


# A made-up family: the Heavy Thorium chain (a smelt at a Forever-only station, an Anvil recipe that needs its
# bar), a Master Forge recipe, a smelt whose bar only a cut recipe uses, and a live chain (a stock-station smelt
# of a raw new ore whose bar a live Anvil recipe with plans uses).
FAMILY_RECIPES = [
    recipe(61901, "Smelt Test Heavy", "70901x1", "70902x1", focus="Molten Foundry", source=2657, skill=186),
    recipe(61902, "Test Heavy Gauntlets", "70902x4,2840x2", "70903x1", plans="70904/61903"),
    recipe(61904, "Test Master Helm", "2840x2", "70905x1", focus="Master Forge"),
    recipe(61905, "Smelt Test Light", "70906x2,3857x1", "70907x1", focus="Forge", source=2657, skill=186),
    recipe(61906, "Test Light Boots", "70907x3,2840x2", "70908x1", plans="70909/61907"),
    recipe(61908, "Smelt Test Orphan", "2840x1", "70910x1", focus="Forge", source=2657, skill=186),
    recipe(61909, "Test Orphan Belt", "70910x2", "70911x1", focus="Master Forge"),
]
FAMILY_ITEMS = [
    item(70901, "Test Heavy Ore", source=2770), item(70902, "Test Heavy Bar", source=2840),
    item(70903, "Test Heavy Gauntlets"), item(70904, "Plans: Test Heavy Gauntlets", source=2881),
    item(70905, "Test Master Helm"), item(70906, "Test Light Ore", source=2770),
    item(70907, "Test Light Bar", source=2840), item(70908, "Test Light Boots"),
    item(70909, "Plans: Test Light Boots", source=2881), item(70910, "Test Orphan Bar", source=2840),
    item(70911, "Test Orphan Belt"),
]
FAMILY_CUT = {
    ("recipe", 61901, "needs a Molten Foundry"), ("recipe", 61904, "needs a Master Forge"),
    ("recipe", 61909, "needs a Master Forge"),
    ("recipe", 61902, "needs Test Heavy Bar (70902), which no remaining recipe makes"),
    ("recipe", 61908, "makes Test Orphan Bar (70910), which no remaining recipe uses"),
    ("item", 70901, "no remaining recipe uses it"), ("item", 70902, "no remaining recipe makes it"),
    ("item", 70903, "no remaining recipe makes it"), ("item", 70904, "teaches a cut recipe"),
    ("item", 70905, "no remaining recipe makes it"), ("item", 70910, "no remaining recipe makes it"),
    ("item", 70911, "no remaining recipe makes it"),
}


def spec_folder(tmp_path, recipes=(), items=(), changes=()):
    """A copy of the spec folder with the recipe and item rows added at the end of their files and the
    (file, old, new) text `changes` made."""
    folder = tmp_path / "spec"
    folder.mkdir()
    for path in SPEC_DIR.glob("*.tsv"):
        text = path.read_text(encoding="utf-8")
        added = {"blacksmithing.tsv": recipes, "items.tsv": items}.get(path.name, ())
        text += "".join(line + "\n" for line in added)
        for file, old, new in changes:
            if file == path.name:
                assert old in text, old
                text = text.replace(old, new, 1)
        (folder / path.name).write_text(text, encoding="utf-8")
    return folder


def run(folder, out):
    return subprocess.run([sys.executable, "-I", str(GENERATOR), "--spec", str(folder), "--stock", str(STOCK),
                           "--out", str(out)], capture_output=True, text=True)


def refused(tmp_path, recipes=(), items=(), changes=()):
    out = tmp_path / "out"
    done = run(spec_folder(tmp_path, recipes, items, changes), out)
    assert done.returncode != 0 and "Traceback" not in done.stderr, done.stderr
    assert not out.exists() or not any(out.rglob("*")), "the generator wrote output"
    return done.stderr


def cut_list(out):
    """{(kind, ID): (name, reason)} of the cut list that the generator wrote."""
    with open(out / "cut-list.tsv", encoding="utf-8") as handle:
        rows = list(csv.DictReader((line for line in handle if not line.startswith("#")), delimiter="\t"))
    return {(row["kind"], int(row["id"])): (row["name"], row["reason"]) for row in rows}


@pytest.fixture(scope="module")
def family(tmp_path_factory):
    tmp = tmp_path_factory.mktemp("family")
    out = tmp / "out"
    done = run(spec_folder(tmp, FAMILY_RECIPES, FAMILY_ITEMS), out)
    assert done.returncode == 0, done.stderr
    return out, done.stdout


# --- The dead-content rule.

def test_the_dead_content_rule_cuts_the_forever_station_recipes_and_everything_with_no_remaining_use(family):
    out, _ = family
    cut = cut_list(out)
    family_cut = {(kind, key, reason) for (kind, key), (_, reason) in cut.items() if key >= 61900 and key < 62000
                  or key >= 70900}
    assert family_cut == FAMILY_CUT


def test_the_generator_prints_the_cut_list(family):
    out, stdout = family
    assert "Dead-content cut list" in stdout
    assert "recipe\t61901\tSmelt Test Heavy\tneeds a Molten Foundry" in stdout
    assert "item\t70902\tTest Heavy Bar\tno remaining recipe makes it" in stdout


def test_cut_rows_reach_neither_the_client_nor_the_server_and_live_ones_do(family):
    out, _ = family
    client, text = read_dbc(out / "client" / "DBFilesClient" / "Spell.dbc")
    sql = (out / "sql" / "forever_world.sql").read_text(encoding="utf-8")
    for spell in (61901, 61902, 61903, 61904, 61908, 61909):
        assert spell not in client and "`entry`=%d," % spell not in sql
    for entry in (70901, 70902, 70903, 70904, 70905, 70910, 70911):
        assert "`entry`=%d," % entry not in sql
    for spell in (61905, 61906, 61907):
        assert spell in client
    for entry in (70906, 70907, 70908, 70909):
        assert "`entry`=%d," % entry in sql


def test_a_plans_item_teaches_its_recipe_through_a_learn_spell(family):
    out, _ = family
    client, text = read_dbc(out / "client" / "DBFilesClient" / "Spell.dbc")
    learn = client[61907]
    assert (learn[EFFECT1], learn[TRIGGER1], text(learn[NAME])) == (LEARN_SPELL, 61906, "Plans: Test Light Boots")
    sql = (out / "sql" / "forever_world.sql").read_text(encoding="utf-8")
    assert ("UPDATE `forever_item_template` SET `spellid_1`=61907, `spelltrigger_1`=0, `spellcharges_1`=-1, "
            "`spellppmrate_1`=0, `spellcooldown_1`=-1, `spellcategory_1`=0, `spellcategorycooldown_1`=-1, "
            "`required_skill`=164, `required_skill_rank`=100 WHERE `entry`=70909;") in sql


def test_two_runs_give_the_same_bytes(family, tmp_path):
    out, _ = family
    again = tmp_path / "again"
    assert run(spec_folder(tmp_path, FAMILY_RECIPES, FAMILY_ITEMS), again).returncode == 0
    for path in sorted(p for p in out.rglob("*") if p.is_file()):
        assert (again / path.relative_to(out)).read_bytes() == path.read_bytes(), path


# --- Refusals.

def test_a_reagent_that_is_no_item_is_refused(tmp_path):
    error = refused(tmp_path, [recipe(61950, "Test Boots", "99999x2", "70950x1")], [item(70950, "Test Boots")])
    assert "recipe Test Boots: spell 61950: the reagent 99999 is no stock item and no item row of the spec" in error


STOCK_ROW = "stock\t2661\t2661\tCopper Chain Belt\t164\t30\t70\t110\t2840x6\t"


def test_a_reagent_of_a_stock_row_that_is_no_item_is_refused(tmp_path):
    error = refused(tmp_path, changes=[("blacksmithing.tsv", STOCK_ROW, STOCK_ROW.replace("2840x6", "99999x6"))])
    assert "recipe Copper Chain Belt: spell 2661: the reagent 99999 is no stock item and no item row of the spec" in error


def test_a_new_reagent_that_a_stock_row_uses_is_not_cut(tmp_path):
    out = tmp_path / "out"
    folder = spec_folder(tmp_path, [recipe(61950, "Smelt Test Bar", "2840x1", "70950x1", focus="Forge", source=2657,
                                           skill=186),
                                    recipe(61951, "Test Master Helm", "70950x2", "70951x1", focus="Master Forge")],
                         [item(70950, "Test Bar", source=2840), item(70951, "Test Master Helm")],
                         [("blacksmithing.tsv", STOCK_ROW, STOCK_ROW.replace("2840x6", "70950x6"))])
    done = run(folder, out)
    assert done.returncode == 0, done.stderr
    cuts = cut_list(out)
    assert ("recipe", 61951) in cuts
    assert ("recipe", 61950) not in cuts and ("item", 70950) not in cuts


def test_a_made_item_that_is_no_item_is_refused(tmp_path):
    error = refused(tmp_path, [recipe(61950, "Test Boots", "2840x2", "79998x1")])
    assert "recipe Test Boots: spell 61950: the made item 79998 is no stock item and no item row of the spec" in error


def test_a_recipe_without_a_source_is_refused(tmp_path):
    row = recipe(61950, "Test Boots", "2840x2", "70950x1").replace("\t1361950\t", "\t-\t")
    error = refused(tmp_path, [row], [item(70950, "Test Boots")])
    assert "recipe Test Boots: spell 61950 has no source (foreverSpell)" in error


def test_an_item_without_a_source_is_refused(tmp_path):
    row = item(70950, "Test Boots").replace("\t370950\t", "\t-\t")
    error = refused(tmp_path, [recipe(61950, "Test Boots", "2840x2", "70950x1")], [row])
    assert "item Test Boots: entry 70950 has no source (foreverItem)" in error


def test_an_item_whose_source_item_is_no_stock_item_is_refused(tmp_path):
    # The #226 follow-up: the copy of a missing source row finds no row, and the item would be silently missing.
    error = refused(tmp_path, [recipe(61950, "Test Boots", "2840x2", "70950x1")], [item(70950, "Test Boots", source=99999)])
    assert "item Test Boots: entry 70950: the source item 99999 is no stock item" in error


def test_plans_that_are_no_item_row_are_refused(tmp_path):
    error = refused(tmp_path, [recipe(61950, "Test Boots", "2840x2", "70950x1", plans="70951/61951")],
                    [item(70950, "Test Boots")])
    assert "recipe Test Boots: spell 61950: the plans 70951 are no item row of the spec" in error


def test_a_learn_spell_outside_the_profession_block_is_refused(tmp_path):
    error = refused(tmp_path, [recipe(61950, "Test Boots", "2840x2", "70950x1", plans="70951/60999")],
                    [item(70950, "Test Boots"), item(70951, "Plans: Test Boots", source=2881)])
    assert "the learn spell 60999 is outside the free profession spell ID range 61000-61999" in error


def test_an_unknown_spell_focus_is_still_refused(tmp_path):
    error = refused(tmp_path, [recipe(61950, "Test Boots", "2840x2", "70950x1", focus="Moonwell Forge")],
                    [item(70950, "Test Boots")])
    assert "no stock SpellFocusObject.dbc name" in error


# --- The real spec (forever/spec).

def by_column(rows, column):
    return {int(row[column]): row for row in rows}


@pytest.fixture(scope="module")
def real(tmp_path_factory):
    out = tmp_path_factory.mktemp("real") / "out"
    done = run(SPEC_DIR, out)
    assert done.returncode == 0, done.stderr
    return out, done.stdout


def new_recipe_rows():
    return [row for row in spec_rows("blacksmithing.tsv") if row["kind"] == "recipe"]


def new_item_rows():
    return [row for row in spec_rows("items.tsv") if row["kind"] == "item"]


def test_the_ticket_counts_of_new_recipes_items_and_cuts(real):
    out, _ = real
    recipes, items = new_recipe_rows(), new_item_rows()
    cut = cut_list(out)
    # 194 new Blacksmithing recipes and the 2 new smelts of build 1.60.1.70685 (#226's tracer is one of them).
    assert len(recipes) == 196
    assert sum(r["skillLine"] == "186" for r in recipes) == 2
    # About 380 items: 195 made items (193 recipes make new gear, Copper Rod the stock 6217, the 2 smelts their
    # bars), the 178 readable plans (193 recipes have plans, 15 of them encrypted) and 9 raw new reagents.
    assert len(items) == 195 + 178 + 9
    assert sum(r["name"].startswith("Plans: ") for r in items) == 178
    # The cut list: the 43 Master Forge recipes, Smelt Heavy Thorium and the 3 Anvil recipes that need Heavy
    # Thorium Bar.
    cut_recipes = {key: reason for (kind, key), (_, reason) in cut.items() if kind == "recipe"}
    assert sum(reason == "needs a Master Forge" for reason in cut_recipes.values()) == 43
    assert len(cut_recipes) == 47


def test_the_cut_list_holds_the_heavy_thorium_and_master_forge_rows(real):
    out, stdout = real
    cut = {(kind, name): reason for (kind, _), (name, reason) in cut_list(out).items()}
    assert cut[("recipe", "Smelt Heavy Thorium")] == "needs a Molten Foundry"
    assert cut[("item", "Heavy Thorium Bar")] == "no remaining recipe makes it"
    assert cut[("item", "Heavy Thorium Ore")] == "no remaining recipe uses it"
    for name in ("Thorium Greatmace", "Thorium Cestus", "Thorium Poleaxe"):
        assert cut[("recipe", name)].startswith("needs Heavy Thorium Bar")
    for name in ("Goregasher Grips", "Clutchlord's Stompers", "Ironwood Blade", "Azerothium Legplates",
                 "Legplates of Glory", "Justice Sabatons", "Enriched Thorium Helm", "Legionite Glaive"):
        assert cut[("recipe", name)] == "needs a Master Forge", name
    assert cut[("item", "Legionite Bar")] == "no remaining recipe uses it"
    assert "recipe\t" in stdout and "Smelt Heavy Thorium\tneeds a Molten Foundry" in stdout


def test_smelt_azerothium_and_pitchblende_survive_for_bold_dirk(real):
    # Bold Dirk (Anvil) needs 10 Azerothium Bar and no Heavy Thorium, so Smelt Azerothium (2 Pitchblende + 1
    # Coal at a Forge) and Pitchblende stay (#232 puts Pitchblende into the Thorium veins).
    out, _ = real
    names = {name for name, _ in cut_list(out).values()}
    assert not {"Smelt Azerothium", "Azerothium Bar", "Pitchblende", "Bold Dirk"} & names
    smelt = next(r for r in new_recipe_rows() if r["name"] == "Smelt Azerothium")
    items = {r["name"]: int(r["entry"]) for r in new_item_rows()}
    assert counted(smelt["reagents"]) == [(3857, 1), (items["Pitchblende"], 2)]
    assert counted(smelt["creates"]) == [(items["Azerothium Bar"], 1)] and smelt["focus"] == "Forge"


@pytest.mark.skipif(not (DUMP / "fv_new_recipes.tsv").is_file(), reason="no client dump at %s" % DUMP)
def test_the_spec_has_every_readable_new_recipe_of_the_client():
    with open(DUMP / "fv_new_recipes.tsv", encoding="utf-8") as handle:
        dump = [r for r in csv.DictReader(handle, delimiter="\t", quoting=csv.QUOTE_NONE) if r["group"] == "fv-new"]
    recipes = by_column(new_recipe_rows(), "foreverSpell")
    items = {int(r["foreverItem"]): int(r["entry"]) for r in new_item_rows()}
    assert set(recipes) == {int(r["spell"]) for r in dump}
    for r in dump:
        row = recipes[int(r["spell"])]
        assert (row["name"], row["yellow"], row["grey"], row["focus"]) == (r["name"], r["yellow"], r["grey"], r["focus"])
        made = int(r["creates_id"])
        assert counted(row["creates"]) == [(items.get(made, made), 1)]


def test_the_encrypted_plans_are_listed_as_excluded_and_have_no_rows():
    encrypted = [251340] + list(range(251342, 251355)) + [251395]
    assert len(encrypted) == 15
    for name in ("blacksmithing.tsv", "items.tsv"):
        text = (SPEC_DIR / name).read_text(encoding="utf-8")
        assert "Excluded (encrypted in the client data, not filled from fan sites): the plans" in text
        for plans in encrypted:
            assert str(plans) in text
    assert not {int(r["foreverItem"]) for r in new_item_rows()} & set(encrypted)


def test_each_live_plans_item_teaches_its_recipe_at_its_skill(real):
    out, _ = real
    client, text = read_dbc(out / "client" / "DBFilesClient" / "Spell.dbc")
    sql = (out / "sql" / "forever_world.sql").read_text(encoding="utf-8")
    cut = {key for (kind, key) in cut_list(out) if kind == "recipe"}
    items = by_column(new_item_rows(), "entry")
    taught = 0
    for row in new_recipe_rows():
        if row["plans"] == "-" or int(row["spell"]) in cut:
            continue
        plans, learn = (int(v) for v in row["plans"].split("/"))
        assert (client[learn][EFFECT1], client[learn][TRIGGER1]) == (LEARN_SPELL, int(row["spell"]))
        assert text(client[learn][NAME]) == items[plans]["name"]
        assert ("`spellid_1`=%d, `spelltrigger_1`=0" % learn) in sql
        assert ("`required_skill`=164, `required_skill_rank`=%s WHERE `entry`=%d;" % (row["minRank"], plans)) in sql
        taught += 1
    # Less the cut recipes, the 15 encrypted plans, and Copper Rod and Smelt Azerothium, which have no plans.
    assert taught == 196 - 47 - 15 - 2


def test_worked_examples_of_the_new_items(real):
    out, _ = real
    server = {row["name"]: row for row in sql_rows(out, "forever_item_new")}
    # Guard's Boots (250504): DefRating 4 = Defense 4 (7517), Sta 5, Str 5, rare, 131 armor.
    boots = server["Guard's Boots"]
    assert (boots["quality"], boots["armor"], boots["spellid_1"]) == ("3", "131", "7517")
    # Brass Knuckles (250602): a fist weapon (item_template class 2, subclass 13), 17-33 damage at 2.4 s.
    fist = server["Brass Knuckles"]
    assert (fist["dmg_min1"], fist["dmg_max1"], fist["delay"]) == ("17", "33", "2400")
    # Bronze Dory: a polearm; Cracked Blacksmith Hammer: a thrown weapon (their copies: Blight, Large Throwing Knife).
    rows = {r["name"]: r for r in new_item_rows()}
    assert rows["Bronze Dory"]["sourceItem"] == "7959" and rows["Cracked Blacksmith Hammer"]["sourceItem"] == "3105"


def test_new_ids_are_in_their_free_ranges_and_unique():
    spell_low, spell_high = free_range("blacksmithing.tsv", "profession spell ID")
    item_low, item_high = free_range("items.tsv", "item ID")
    spells = []
    for row in new_recipe_rows():
        spells.append(int(row["spell"]))
        if row["plans"] != "-":
            spells.append(int(row["plans"].split("/")[1]))
    entries = [int(row["entry"]) for row in new_item_rows()]
    assert all(spell_low <= s <= spell_high for s in spells) and len(set(spells)) == len(spells)
    assert all(item_low <= e <= item_high for e in entries) and len(set(entries)) == len(entries)
