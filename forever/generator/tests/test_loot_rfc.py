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
from test_pipeline import ROOT, STAT_TYPES, STOCK, bonus_of, generate, read_dbc

pytestmark = pytest.mark.skipif(not (STOCK / "Spell.dbc").is_file(), reason="no stock DBC folder at %s" % STOCK)

ITEMS, LOOT = "loot_ragefire_chasm_items.tsv", "loot_ragefire_chasm.tsv"
OGGLEFLINT, JERGOSH, BAZZALAN, TARAGAMAN = 11517, 11518, 11519, 11520
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
    assert loot_rows(text) == {key: (chance, 0, 1, 1, 0, 10) for key, chance in DROPS.items()}
    assert copied_entries(text) == {OGGLEFLINT, BAZZALAN}
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
