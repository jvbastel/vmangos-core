"""The Forever ores in the mining veins (jvbastel/WOWAI#232, spec #225).

forever/spec/mining.tsv holds the new ore items (Pyrite, Bauxite) and one drop row per ore and vein loot ID. The
generator writes forever_gameobject_loot_template: for each loot ID a copy of all its stock rows (the core reads
the table over the stock one by `entry`, so a loot ID needs its full set) plus the ore row. The test runs the
generator on the spec folder and reads the SQL and the cut list from the outside.

The expected rates come from the spec (#225 "Ores") and the stock world DB (notes/forever/explore-professions.md
5a, and the gem rows of the Iron, Mithril and Thorium loot IDs read on the batch slot on 2026-10-30).
"""
import re

import pytest

from test_blacksmithing import refused as refused_in, spec_rows
from test_new_recipes import cut_list, run, spec_folder
from test_pipeline import ROOT, STOCK

pytestmark = pytest.mark.skipif(not (STOCK / "Spell.dbc").is_file(), reason="no stock DBC folder at %s" % STOCK)

PYRITE, BAUXITE, PITCHBLENDE = 70382, 70383, 70002
# (loot ID, ore): (chance in percent, loot group, rate kind). Pyrite: the Malachite/Tigerseye rate of the Copper
# veins (1%, group 1, with the stock gems), also in the Tin veins. Bauxite and Pitchblende: the stock gem rate of
# their veins, a fallback until launch data gives one.
DROPS = {
    (1502, PYRITE): (1.0, 1, "spec"), (1735, PYRITE): (1.0, 1, "spec"), (2626, PYRITE): (1.0, 1, "spec"),
    (1503, PYRITE): (1.0, 1, "spec"), (1736, PYRITE): (1.0, 1, "spec"), (2627, PYRITE): (1.0, 1, "spec"),
    (1505, BAUXITE): (1.0, 0, "fallback"), (1742, BAUXITE): (0.7, 0, "fallback"), (13961, BAUXITE): (0.7, 0, "fallback"),
    (9597, PITCHBLENDE): (0.7, 0, "fallback"), (13960, PITCHBLENDE): (0.7, 0, "fallback"),
    (12883, PITCHBLENDE): (0.8, 0, "fallback"),
}
# The Copper Vein of the in-game check and its loot ID.
COPPER_VEIN, COPPER_LOOT = 1731, 1502
TABLE = "forever_gameobject_loot_template"
MARKER = "forever_removed"


def sql(out):
    return (out / "sql" / "forever_world.sql").read_text(encoding="utf-8")


def loot_rows(text):
    """{(loot ID, item): (chance, group, min count, max count)} of the ore rows in the generated SQL."""
    rows = {}
    for values in re.findall(r"^INSERT INTO `%s` \(`entry`, `item`, `ChanceOrQuestChance`, `groupid`, `mincountOrRef`, "
                             r"`maxcount`\) VALUES \(([^)]*)\);$" % TABLE, text, re.M):
        entry, item, chance, group, low, high = values.split(", ")
        rows[(int(entry), int(item))] = (float(chance), int(group), int(low), int(high))
    return rows


def copied_entries(text):
    return {int(e) for e in re.findall(r"^INSERT INTO `%s` SELECT \* FROM `gameobject_loot_template` WHERE `entry`=(\d+);$"
                                       % TABLE, text, re.M)}


# --- The spec file.

def test_the_mining_spec_has_the_ore_items_and_their_vein_drops():
    rows = spec_rows("mining.tsv")
    ores = {int(row["item"]): row for row in rows if row["kind"] == "ore"}
    assert {(e, r["name"], r["foreverItem"]) for e, r in ores.items()} == {
        (PYRITE, "Pyrite", "249391"), (BAUXITE, "Bauxite", "249425")}
    drops = {(int(row["lootId"]), int(row["item"])): (float(row["chance"]), int(row["groupid"]), row["rate"])
             for row in rows if row["kind"] == "drop"}
    assert drops == DROPS


def test_the_copper_vein_of_the_in_game_check_uses_its_loot_id():
    copper = [row for row in spec_rows("mining.tsv") if row["kind"] == "drop" and int(row["lootId"]) == COPPER_LOOT]
    assert str(COPPER_VEIN) in copper[0]["gameobjects"].split(",")


# --- The generated server rows.

def test_each_drop_is_a_loot_row_on_top_of_a_copy_of_the_stock_rows_of_its_loot_id(out):
    text = sql(out)
    assert loot_rows(text) == {key: (chance, group, 1, 1) for key, (chance, group, _) in DROPS.items()}
    assert copied_entries(text) == {entry for entry, _ in DROPS}


def test_the_ore_row_comes_after_the_copy_of_its_loot_id(out):
    # The stock rows of a loot ID are copied first: the ore row is not one of them, so the copy cannot clash.
    text = sql(out)
    for entry, item in DROPS:
        copy = text.index("SELECT * FROM `gameobject_loot_template` WHERE `entry`=%d;" % entry)
        assert text.index("VALUES (%d, %d, " % (entry, item)) > copy


def test_the_sql_fails_when_a_vein_does_not_use_its_loot_id(out):
    # The generator has no world DB: the SQL checks the gameobjects of each drop row when the realm loads it.
    guard = re.search(r"^SELECT IF\(\(SELECT COUNT\(DISTINCT `entry`\) FROM `gameobject_template` WHERE `entry` IN "
                      r"\(([\d, ]+)\) AND `data1`=%d\)=(\d+), 0, \(SELECT 1 UNION SELECT 2\)\) INTO @forever_vein;$"
                      % COPPER_LOOT, sql(out), re.M)
    assert guard and str(COPPER_VEIN) in guard.group(1).split(", ")
    assert int(guard.group(2)) == len(guard.group(1).split(", "))


def test_the_new_ores_are_items_copied_from_a_stock_ore(out):
    text = sql(out)
    assert "-- Item Pyrite (%d), from item 2776." % PYRITE in text
    assert "-- Item Bauxite (%d), from item 7911." % BAUXITE in text


def test_the_loot_table_is_an_override_table_with_the_removal_marker(out):
    text = sql(out)
    assert "CREATE TABLE `%s` LIKE `gameobject_loot_template`;" % TABLE in text
    alter = "ALTER TABLE `%s` ADD COLUMN `%s` TINYINT UNSIGNED NOT NULL DEFAULT 0;" % (TABLE, MARKER)
    assert text.index(alter) > max(m.start() for m in re.finditer("INSERT INTO `%s`" % TABLE, text))


def test_the_cut_list_holds_the_heavy_thorium_vein_and_its_ore(out):
    cuts = cut_list(out)
    assert ("mining", 4170) in cuts and "Heavy Thorium Vein" in cuts[("mining", 4170)][0]
    assert ("item", 70361) in cuts  # Heavy Thorium Ore (#230): only Smelt Heavy Thorium (Molten Foundry) uses it


def test_a_drop_of_an_ore_that_the_dead_content_rule_cuts_is_left_out(tmp_path):
    # Smelt Azerothium is the one recipe that keeps Pitchblende alive (#230). At a Molten Foundry it is cut, and
    # then Pitchblende and its drops are cut too.
    out = tmp_path / "out"
    folder = spec_folder(tmp_path, changes=[("blacksmithing.tsv", "3857x1,70002x2\t70001x1\tForge",
                                             "3857x1,70002x2\t70001x1\tMolten Foundry")])
    done = run(folder, out)
    assert done.returncode == 0, done.stderr
    rows = loot_rows(sql(out))
    assert not [key for key in rows if key[1] == PITCHBLENDE]
    assert (1502, PYRITE) in rows
    cuts = cut_list(out)
    assert {key for key in cuts if key[0] == "drop"} == {("drop", 9597), ("drop", 13960), ("drop", 12883)}
    assert ("item", PITCHBLENDE) in cuts


# --- Refusals.

def refused(tmp_path, *changes):
    return refused_in(tmp_path, "mining.tsv", *changes)


def test_a_drop_of_no_item_is_refused(tmp_path):
    error = refused(tmp_path, ("drop\t%d\tBauxite\t" % BAUXITE, "drop\t79998\tBauxite\t"))
    assert "item 79998 is no item" in error


def test_the_same_ore_twice_in_a_loot_id_is_refused(tmp_path):
    rows = [row for row in spec_rows("mining.tsv") if row["kind"] == "drop" and row["lootId"] == "1505"]
    line = "\t".join(rows[0].values())
    error = refused(tmp_path, (line, line + "\n" + line))
    assert "loot ID 1505 drops item %d twice" % BAUXITE in error


def test_an_unknown_rate_kind_is_refused(tmp_path):
    error = refused(tmp_path, ("\tfallback\t", "\tguess\t"))
    assert "rate 'guess'" in error


def test_a_chance_outside_0_to_100_is_refused(tmp_path):
    error = refused(tmp_path, ("\t0.8\t0\tfallback\t", "\t120\t0\tfallback\t"))
    assert "chance 120" in error


# --- The core and the migration (read: the batch build runs them).

def test_the_migration_makes_the_loot_override_table_without_the_marker():
    # The generator owns the marker (removals.marker_sql); forever_world.sql makes the table again.
    migrations = [p for p in (ROOT / "sql" / "migrations").glob("*_world.sql")
                  if TABLE in p.read_text(encoding="utf-8")]
    assert len(migrations) == 1
    text = migrations[0].read_text(encoding="utf-8")
    assert "CREATE TABLE IF NOT EXISTS `%s` LIKE `gameobject_loot_template`;" % TABLE in text
    assert MARKER not in text
    allowlist = (ROOT / "wowai" / "core-allowlist.txt").read_text(encoding="utf-8")
    assert "sql/migrations/%s" % migrations[0].name in allowlist.splitlines()


def test_the_core_needs_the_table_with_the_rules_on_and_reads_gameobject_loot_through_it():
    forever = (ROOT / "src" / "game" / "Forever" / "Forever.cpp").read_text(encoding="utf-8")
    assert '"%s"' % TABLE in forever
    loot = (ROOT / "src" / "game" / "LootMgr.cpp").read_text(encoding="utf-8")
    # #242 widens the hook to creature and reference loot (test_loot_rfc.py).
    assert "this == &LootTemplates_Gameobject ||" in loot and "? Forever::Table(GetName(), \"entry\")" in loot
