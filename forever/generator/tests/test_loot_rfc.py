"""The Forever loot of Ragefire Chasm (jvbastel/WOWAI#242, spec #240, testing decision 1).

The spec rows are in forever/spec/loot_ragefire_chasm.tsv (drops, quest rewards) and loot_ragefire_chasm_items.tsv
(items). The test runs the generator on the spec folder and reads the generated SQL and the loot list from the
outside. The expected values come from Wowhead's Forever tooltips (read 2026-10-31, notes/forever/wowhead-cache) and
the stock world database (notes/forever/dungeon-loot-research.md section 2: the RFC boss loot entries and references).

Copy this file for a new dungeon: change the file names and the expected rows, keep the tests.
"""
import re

import pytest

from test_blacksmithing import spec_rows, sql_rows
from test_new_recipes import run, spec_folder
from test_pipeline import ROOT, STAT_TYPES, STOCK, bonus_of, generate, read_dbc

pytestmark = pytest.mark.skipif(not (STOCK / "Spell.dbc").is_file(), reason="no stock DBC folder at %s" % STOCK)

ITEMS, LOOT = "loot_ragefire_chasm_items.tsv", "loot_ragefire_chasm.tsv"
OGGLEFLINT, JERGOSH, BAZZALAN, TARAGAMAN = 11517, 11518, 11519, 11520
# The other dungeon files add their own rows to the same output (#243-#247): the RFC tests look at the RFC rows.
RFC_BOSSES = {OGGLEFLINT, JERGOSH, BAZZALAN, TARAGAMAN}
RFC_QUESTS = {5722, 5723, 5724, 5725, 5728, 5730, 5761}
# The stock boss drops of Taragaman and Jergosh with their Forever values (Wowhead): quality, armor, base stats,
# (damage, delay) of a weapon.
STOCK_DROPS = {
    14145: ("Cursed Felblade", 3, 0, {}, ((19, 37), 2300)),
    14147: ("Cavedweller Bracers", 3, 78, {"Str": 2, "Spi": 3}, None),
    14148: ("Crystalline Cuffs", 3, 15, {"Sta": 2, "Spi": 3}, None),
    14149: ("Subterranean Cape", 3, 17, {"Str": 3}, None),
    14150: ("Robe of Evocation", 3, 35, {"Sta": 6, "Int": 4, "Spi": 4}, None),
    14151: ("Chanting Blade", 3, 0, {"Agi": 2, "Sta": 3}, ((12, 24), 1500)),
}

# The new items (Wowhead Forever tooltips): entry: (name, Forever item, (class, subclass, slot) of Item.db2, quality,
# base stats, on-equip bonuses as (bonus, value), (damage, delay) of a weapon).
NEW_ITEMS = {
    71000: ("Trogg Scepter", 272996, (2, 4, 21), 3, {"Spi": 4}, {("SpellPower", 11), ("Healing", 22)}, ((13, 26), 2400)),
    71001: ("Bone Knuckles", 272998, (2, 13, 21), 3, {}, {("AP", 6)}, ((17, 32), 2000)),
    71002: ("Barbaric Crossbow", 272999, (2, 18, 26), 3, {}, set(), ((15, 23), 2000)),
    71003: ("Searing Dagger", 273003, (2, 15, 21), 3, {"Spi": 4}, set(), ((9, 17), 1600)),
    71004: ("Satyrskin Cloak", 273005, (4, 1, 16), 3, {"Sta": 3}, set(), None),
    71005: ("Chasm Walkers", 273007, (4, 2, 8), 3, {"Spi": 5}, {("AP", 4)}, None),
    71006: ("Garrison Cuffs", 270003, (4, 3, 9), 2, {"Spi": 2}, {("SpellPower", 1), ("Healing", 2)}, None),
}
ON_EQUIP = "1"
# The new boss drops (Wowhead Forever "Dropped by" and "Drop Chance"): (creature loot entry, item): chance. The loot
# entry of each RFC boss is its creature entry (stock creature_template.loot_id).
DROPS = {(OGGLEFLINT, 71000): 3.22, (OGGLEFLINT, 71001): 2.65, (OGGLEFLINT, 71002): 2.93,
         (BAZZALAN, 71003): 2.84, (BAZZALAN, 71004): 2.89, (BAZZALAN, 71005): 2.82}
# The stock unique drops of Taragaman and Jergosh: one equal-chance group (groupid 1) in a reference loot entry.
TARAGAMAN_REFERENCE, JERGOSH_REFERENCE = 30332, 30331
# Quest 5724 Returning the Lost Satchel: stock choices 15452 and 15453 (stock world DB), Forever adds Garrison Cuffs
# (Wowhead Forever quest page).
SATCHEL, GARRISON_CUFFS = 5724, 71006
QUEST = "forever_quest_template"
CREATURE_LOOT, REFERENCE_LOOT = "forever_creature_loot_template", "forever_reference_loot_template"
LOOT_ROW = (r"^INSERT INTO `%s` \(`entry`, `item`, `ChanceOrQuestChance`, `groupid`, `mincountOrRef`, `maxcount`, "
            r"`patch_min`, `patch_max`\) VALUES \(([^)]*)\);$")
# The stock world data of the generator: the stock gear that a new item may copy.
GEAR = ROOT / "forever" / "generator" / "forever_gen" / "stock_world" / "gear.tsv"


def stock_gear():
    """{entry: (class, subclass, slot, display)} of the stock gear file."""
    rows = [line.split("\t") for line in GEAR.read_text(encoding="utf-8").splitlines()[2:]]
    return {int(r[0]): (int(r[1]), int(r[2]), int(r[3]), int(r[6])) for r in rows}


@pytest.fixture(scope="module")
def out(tmp_path_factory):
    return generate(tmp_path_factory.mktemp("forever242"))


def sql(out):
    return (out / "sql" / "forever_world.sql").read_text(encoding="utf-8")


def item_rows(out, temp):
    """{entry: {column: value}} of the item rows on the temporary copy table `temp` (forever_item_stock: changed
    stock items, forever_item_new: new items)."""
    return {int(r["entry"]): r for r in sql_rows(out, temp) if "entry" in r}


def loot_rows(text, table=CREATURE_LOOT):
    """{(entry, item): (chance, group, min count, max count, patch_min, patch_max)} of the new loot rows."""
    rows = {}
    for values in re.findall(LOOT_ROW % table, text, re.M):
        entry, item, chance, *rest = values.split(", ")
        rows[(int(entry), int(item))] = (float(chance),) + tuple(int(v) for v in rest)
    return rows


def copied_entries(text, table=CREATURE_LOOT):
    """The loot entries whose stock rows the SQL copies into `table`."""
    return {int(e) for e in re.findall(r"^INSERT INTO `%s` SELECT \* FROM `%s` WHERE `entry`=(\d+);$"
                                       % (table, table[len("forever_"):]), text, re.M)}


def stats_of(row):
    """{stat name: value} of the stat slots of a generated item row."""
    names = {str(v): k for k, v in STAT_TYPES.items()}
    return {names[row["stat_type%d" % k]]: int(row["stat_value%d" % k]) for k in range(1, 11)
            if row["stat_type%d" % k] != "0"}


# --- The spec files.

def test_every_loot_row_names_its_source_and_date():
    for name in (ITEMS, LOOT):
        rows = spec_rows(name)
        assert rows
        for row in rows:
            assert row["source"].startswith(("https://nether.wowhead.com/", "https://www.wowhead.com/forever/", "client "))
            assert re.fullmatch(r"\d{4}-\d{2}-\d{2}", row["date"])


# --- Items.

def test_the_stock_boss_drops_get_their_forever_stats(out):
    rows = item_rows(out, "forever_item_stock")
    for entry, (name, quality, armor, stats, weapon) in STOCK_DROPS.items():
        row = rows[entry]
        assert (row["name"], int(row["quality"]), int(row["armor"]), stats_of(row)) == (name, quality, armor, stats)
        if weapon:
            assert ((int(row["dmg_min1"]), int(row["dmg_max1"])), int(row["delay"])) == weapon


def test_a_new_item_copies_a_stock_item_of_its_class_subclass_and_slot_and_takes_its_display(out):
    text, gear = sql(out), stock_gear()
    displays, _ = read_dbc(STOCK / "ItemDisplayInfo.dbc")
    rows = item_rows(out, "forever_item_new")
    for entry, (name, _, kind, quality, _, _, _) in NEW_ITEMS.items():
        source = int(re.search(r"^-- Item %s \(%d\), from item (\d+)\.$" % (re.escape(name), entry), text, re.M).group(1))
        assert gear[source][:3] == kind, name
        assert int(rows[entry]["display_id"]) == gear[source][3] and gear[source][3] in displays
        assert (rows[entry]["name"], int(rows[entry]["quality"])) == (name, quality)


def test_the_new_items_have_their_forever_stats_as_stock_equip_spells(out):
    spells, _ = read_dbc(STOCK / "Spell.dbc")
    rows = item_rows(out, "forever_item_new")
    for entry, (name, _, _, _, stats, bonuses, weapon) in NEW_ITEMS.items():
        row = rows[entry]
        assert stats_of(row) == stats, name
        equip = {bonus_of(spells, int(row["spellid_%d" % k])) for k in range(1, 6)
                 if row["spelltrigger_%d" % k] == ON_EQUIP}
        assert equip == bonuses, name
        if weapon:
            assert ((int(row["dmg_min1"]), int(row["dmg_max1"])), int(row["delay"])) == weapon


def test_a_new_or_now_blue_item_disenchants_as_a_stock_blue_item_of_its_level(out):
    # Stock blue weapons and armor of item level 20-25 disenchant by disenchant_loot_template 41 (stock world DB);
    # a copied green or level-51 row would give the wrong materials.
    rows = {**item_rows(out, "forever_item_new"), **item_rows(out, "forever_item_stock")}
    blue = [e for e, item in NEW_ITEMS.items() if item[3] == 3] + list(STOCK_DROPS)
    assert {e: int(rows[e]["disenchant_id"]) for e in blue} == dict.fromkeys(blue, 41)



# --- Boss drops.

def test_each_new_drop_is_a_row_outside_the_stock_groups_on_a_copy_of_the_boss_loot(out):
    text = sql(out)
    # Group 0 (no equal-chance group), one item, from patch 0 (Ragefire Chasm is open from the first patch) to 10.
    assert {k: v for k, v in loot_rows(text).items() if k[0] in RFC_BOSSES} == {
        key: (chance, 0, 1, 1, 0, 10) for key, chance in DROPS.items()}
    assert copied_entries(text) & RFC_BOSSES == {OGGLEFLINT, BAZZALAN}
    for entry, item in DROPS:
        copy = text.index("INSERT INTO `%s` SELECT * FROM `creature_loot_template` WHERE `entry`=%d;" % (CREATURE_LOOT, entry))
        assert text.index("VALUES (%d, %d, " % (entry, item)) > copy


def test_the_stock_equal_chance_groups_of_taragaman_and_jergosh_stay_stock(out):
    # Their stock drops keep the stock chance: no Forever row replaces their creature loot or their reference.
    text = sql(out)
    assert not {TARAGAMAN, JERGOSH} & copied_entries(text)
    assert not copied_entries(text, REFERENCE_LOOT) and not loot_rows(text, REFERENCE_LOOT)


def test_the_sql_fails_when_a_boss_does_not_use_its_loot_entry(out):
    guard = ("SELECT IF((SELECT COUNT(*) FROM `creature_template` WHERE `entry`=%d)>0 AND (SELECT COUNT(*) FROM "
             "`creature_template` WHERE `entry`=%d AND `loot_id`<>%d)=0, 0, (SELECT 1 UNION SELECT 2)) INTO @forever_boss;")
    for boss in (OGGLEFLINT, BAZZALAN):
        assert guard % (boss, boss, boss) in sql(out)



# --- Quest rewards.

def quest_changes(out):
    """{quest: {column: value}} of the reward changes on the quest copies."""
    changes = {}
    for m in re.finditer(r"^-- Quest (\d+) .*\n(?:.*\n)*?UPDATE `forever_quest_new` SET (.*);$", sql(out), re.M):
        changes[int(m.group(1))] = dict(re.findall(r"`(\w+)`=(\d+)", m.group(2)))
    return changes


def test_quest_5724_offers_garrison_cuffs_as_its_third_choice(out):
    changes = {q: c for q, c in quest_changes(out).items() if q in RFC_QUESTS}
    assert changes == {SATCHEL: {"RewChoiceItemId3": str(GARRISON_CUFFS), "RewChoiceItemCount3": "1"}}


def test_the_quest_copy_takes_every_stock_row_of_the_quest_and_fails_on_other_stock_choices(out):
    text = sql(out)
    assert "CREATE TEMPORARY TABLE `forever_quest_new` SELECT * FROM `quest_template` WHERE `entry`=%d;" % SATCHEL in text
    assert ("SELECT IF((SELECT COUNT(*) FROM `forever_quest_new`)>0 AND (SELECT COUNT(*) FROM `forever_quest_new` WHERE "
            "NOT (`RewChoiceItemId1`=15452 AND `RewChoiceItemId2`=15453 AND `RewChoiceItemId3`=0 AND `RewChoiceItemId4`=0 "
            "AND `RewChoiceItemId5`=0 AND `RewChoiceItemId6`=0))=0, 0, (SELECT 1 UNION SELECT 2)) INTO @forever_quest;") in text
    assert "INSERT INTO `%s` SELECT * FROM `forever_quest_new`;" % QUEST in text


def test_the_reward_items_of_the_rfc_quests_get_their_forever_stats(out):
    rows = item_rows(out, "forever_item_stock")
    # Wowhead Forever: (quality, base stats) of the stock rewards of 5724, 5725 and 5730.
    expected = {15453: (2, {"Sta": 3}), 15449: (2, {"Sta": 3, "Int": 3}), 15450: (2, {"Sta": 3, "Spi": 3}),
                15451: (2, {"Str": 3, "Spi": 3}), 15443: (3, {"Sta": 4}), 15445: (3, {"Str": 3, "Spi": 3}),
                15424: (3, {"Sta": 6, "Spi": 6}), 15444: (3, {"Agi": 6, "Int": 6})}
    assert {e: (int(rows[e]["quality"]), stats_of(rows[e])) for e in expected} == expected



# --- The lists and the same bytes.

def loot_list(out):
    lines = (out / "loot-list.tsv").read_text(encoding="utf-8").splitlines()[2:]
    return {tuple(line.split("\t")[:3]): line.split("\t")[4] for line in lines}


def test_the_loot_list_names_each_left_out_forever_effect(out):
    listed = loot_list(out)
    rfc_items = {row["entry"] for row in spec_rows(ITEMS)}
    assert {key for key in listed if key[0] == "left-out" and key[2] in rfc_items} == {
        ("left-out", "item", "14145"), ("left-out", "item", "71001"), ("left-out", "item", "71002"),
        ("left-out", "item", "71003")}
    assert "Attack Power against Beasts" in listed[("left-out", "item", "71001")]


def changed(tmp_path, *changes, extra=()):
    """The output folder of the generator on the spec folder with the (file, old, new) text changes and the extra
    (file, line) lines, and its run."""
    folder = spec_folder(tmp_path, changes=changes)
    for name, line in extra:
        with open(folder / name, "a", encoding="utf-8") as handle:
            handle.write(line + "\n")
    out = tmp_path / "out"
    return out, run(folder, out)


def test_an_unconfirmed_drop_and_an_unreadable_item_are_left_out_and_listed(tmp_path):
    out, done = changed(tmp_path, (LOOT, "2.65\t-\t-\tconfirmed", "2.65\t-\t-\tunconfirmed"),
                        (ITEMS, "\t-\tconfirmed\thttps://nether.wowhead.com/tooltip/item/273005",
                         "\t-\tunreadable\thttps://nether.wowhead.com/tooltip/item/273005"))
    assert done.returncode == 0, done.stderr
    listed = loot_list(out)
    assert ("unconfirmed", "drop", "71001") in listed
    assert ("unreadable", "item", "71004") in listed and ("unreadable", "drop", "71004") in listed
    rows = loot_rows(sql(out))
    assert (OGGLEFLINT, 71001) not in rows and (BAZZALAN, 71004) not in rows and (BAZZALAN, 71003) in rows
    assert 71004 not in item_rows(out, "forever_item_new")


def test_two_runs_give_the_same_bytes(out, tmp_path):
    again = generate(tmp_path / "again")
    files = sorted(p.relative_to(out) for p in out.rglob("*") if p.is_file())
    assert files == sorted(p.relative_to(again) for p in again.rglob("*") if p.is_file())
    assert all((out / f).read_bytes() == (again / f).read_bytes() for f in files)


# --- Other row cases that the RFC rows do not use.

def test_a_drop_into_a_reference_that_only_the_boss_uses_copies_the_reference(tmp_path):
    out, done = changed(tmp_path, extra=[(LOOT, "\t".join([
        "drop", "71004", "Satyrskin Cloak", str(TARAGAMAN), "Taragaman the Hungerer", "reference %d" % TARAGAMAN_REFERENCE,
        "5", "-", "-", "confirmed", "https://nether.wowhead.com/tooltip/item/273005?dataEnv=16&locale=0", "2026-10-31", "test"]))])
    assert done.returncode == 0, done.stderr
    text = sql(out)
    assert copied_entries(text, REFERENCE_LOOT) == {TARAGAMAN_REFERENCE}
    assert loot_rows(text, REFERENCE_LOOT) == {(TARAGAMAN_REFERENCE, 71004): (5.0, 0, 1, 1, 0, 10)}


def test_a_swapped_choice_and_a_lifted_class_lock(tmp_path):
    source = "https://www.wowhead.com/forever/quest=5725"
    out, done = changed(tmp_path, extra=[
        (LOOT, "\t".join(["reward", "71005", "Chasm Walkers", "5725", "The Power to Destroy...", "-", "-", "15450", "1",
                          "confirmed", source, "2026-10-31", "test"])),
        (LOOT, "\t".join(["unlock", "15449", "Ghastly Trousers", "5725", "The Power to Destroy...", "-", "-", "-", "-",
                          "confirmed", source, "2026-10-31", "test"]))])
    assert done.returncode == 0, done.stderr
    assert quest_changes(out)[5725] == {"RewChoiceItemId2": "71005", "RewChoiceItemCount2": "1"}
    assert item_rows(out, "forever_item_stock")[15449]["allowable_class"] == "-1"


# --- Refusals: the generator stops with an error and writes nothing.

def refused(tmp_path, *changes, extra=()):
    out, done = changed(tmp_path, *changes, extra=extra)
    assert done.returncode != 0 and "Traceback" not in done.stderr, done.stderr
    assert not out.exists() or not any(out.rglob("*")), "the generator wrote output"
    return done.stderr


def test_a_drop_with_no_source_is_refused(tmp_path):
    error = refused(tmp_path, (LOOT, "\thttps://nether.wowhead.com/tooltip/item/272996?dataEnv=16&locale=0\t", "\t-\t"))
    assert "Trogg Scepter" in error and "has no source" in error


def test_an_item_with_no_source_is_refused(tmp_path):
    error = refused(tmp_path, (ITEMS, "\thttps://nether.wowhead.com/tooltip/item/14147?dataEnv=16&locale=0\t", "\t\t"))
    assert "Cavedweller Bracers" in error and "has no source" in error


def test_an_item_id_clash_is_refused(tmp_path):
    error = refused(tmp_path, (ITEMS, "new\t71001\t272998", "new\t71000\t272998"))
    assert "entry 71000 is in the spec twice" in error


def test_a_new_item_outside_the_block_of_its_file_is_refused(tmp_path):
    error = refused(tmp_path, (ITEMS, "new\t71001\t272998", "new\t71101\t272998"),
                    (LOOT, "drop\t71001\t", "drop\t71101\t"))
    assert "outside the item ID block 71000-71099" in error


def test_a_non_stock_display_is_refused(tmp_path):
    error = refused(tmp_path, (ITEMS, "\t-\t1342\t", "\t999999\t1342\t"))
    assert "the display 999999 is no stock ItemDisplayInfo.dbc ID" in error


def test_an_unknown_boss_is_refused(tmp_path):
    error = refused(tmp_path, (LOOT, "71000\tTrogg Scepter\t11517\t", "71000\tTrogg Scepter\t99999\t"))
    assert "unknown boss" in error


def test_a_boss_under_another_name_is_refused(tmp_path):
    error = refused(tmp_path, (LOOT, "71000\tTrogg Scepter\t11517\tOggleflint", "71000\tTrogg Scepter\t11517\tOgleflint"))
    assert "is 'Oggleflint', not 'Ogleflint'" in error


def test_an_unknown_quest_is_refused(tmp_path):
    error = refused(tmp_path, (LOOT, "Garrison Cuffs\t5724\t", "Garrison Cuffs\t99999\t"))
    assert "unknown quest" in error


def test_a_drop_of_an_unknown_item_is_refused(tmp_path):
    error = refused(tmp_path, (LOOT, "drop\t71000\tTrogg Scepter", "drop\t79990\tTrogg Scepter"))
    assert "item 79990 is no item" in error


def test_a_reference_that_other_creatures_use_is_refused(tmp_path):
    error = refused(tmp_path, (LOOT, "Oggleflint\tcreature\t3.22", "Oggleflint\treference 30020\t3.22"))
    assert "reference 30020 is used by the creature loot" in error


# --- The core and the migration (read: the batch build runs them).

def test_the_core_reads_creature_reference_and_quest_rows_through_the_forever_tables():
    loot = (ROOT / "src" / "game" / "LootMgr.cpp").read_text(encoding="utf-8")
    assert re.search(r"this == &LootTemplates_Gameobject \|\| this == &LootTemplates_Creature \|\| "
                     r"this == &LootTemplates_Reference\s*\?\s*Forever::Table\(GetName\(\), \"entry\"\)", loot)
    quests = (ROOT / "src" / "game" / "ObjectMgr.cpp").read_text(encoding="utf-8")
    load = quests[quests.index("void ObjectMgr::LoadQuests()"):]
    load = load[:load.index("if (!result)")]
    assert 'Forever::SelectWithForeverRows("quest_template", "entry"' in load


def test_one_migration_makes_the_three_tables_and_is_on_the_allowlist():
    migrations = [p for p in (ROOT / "sql" / "migrations").glob("*_world.sql") if QUEST in p.read_text(encoding="utf-8")]
    assert len(migrations) == 1
    text = migrations[0].read_text(encoding="utf-8")
    for table, stock in ((CREATURE_LOOT, "creature_loot_template"), (REFERENCE_LOOT, "reference_loot_template"),
                         (QUEST, "quest_template")):
        assert "CREATE TABLE IF NOT EXISTS `%s` LIKE `%s`;" % (table, stock) in text
    assert "forever_removed" not in text
    allowlist = (ROOT / "wowai" / "core-allowlist.txt").read_text(encoding="utf-8")
    assert "sql/migrations/%s" % migrations[0].name in allowlist.splitlines()
