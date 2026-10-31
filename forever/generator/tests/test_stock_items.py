"""The changed stock crafted items of jvbastel/WOWAI#227 (spec #225) in the generated data.

It runs the generator on the spec folder and reads the server SQL that it wrote: each `stock` row of
forever/spec/items.tsv must become a forever_item_template row with its Forever values, and each bonus must be
a stock equip spell that gives that bonus (read from the stock Spell.dbc, not from the generator's table).
The refusal tests change one spec row and expect the generator to fail and write nothing.

The completeness test compares the rows with the dump of the Forever client (#224), which stays outside git
(FOREVER_CLIENT_DUMP); without it, that test skips.
"""
import csv
import os
import re
from pathlib import Path

import pytest

from test_blacksmithing import loot_item_rows, refused, spec_rows
from test_pipeline import DUMP, STAT_TYPES, STOCK, bonus_of, generate, int32, read_dbc  # noqa: F401

pytestmark = pytest.mark.skipif(not (STOCK / "Spell.dbc").is_file(), reason="no stock DBC folder at %s" % STOCK)


QUALITIES = {"poor": 0, "common": 1, "uncommon": 2, "rare": 3, "epic": 4}
BONDINGS = {"none": 0, "pickup": 1, "equip": 2, "use": 3}
SLOTS = {"oneHand": 13, "mainHand": 21, "offHand": 22, "twoHand": 17, "shield": 14, "ranged": 15, "holdable": 23}
RESISTANCES = ("holy", "fire", "nature", "frost", "shadow", "arcane")
ON_EQUIP = 1


def stock_items(out):
    """{entry: {"set": {column: value}, "equip": [spell IDs put on equip, in order], "clears": bool}} of the
    `stock` item blocks of the generated SQL."""
    sql = (out / "sql" / "forever_world.sql").read_text(encoding="utf-8")
    items = {}
    for block in re.findall(r"^-- Changed stock item .*?^DROP TEMPORARY TABLE `forever_item_stock`;$", sql, re.M | re.S):
        entry = int(re.search(r"WHERE `entry`=(\d+) ORDER BY `patch` DESC LIMIT 1;", block).group(1))
        first = re.search(r"^UPDATE `forever_item_stock` SET (.*);$", block, re.M).group(1)
        values = dict(re.findall(r"`(\w+)`=('(?:[^'\\]|\\.)*'|-?\d+(?:\.\d+)?)", first))
        items[entry] = {
            "set": {k: v.strip("'") for k, v in values.items()},
            "equip": [int(s) for s in re.findall(r"SET `spellid_1`=IF\(@forever_slot=1, (\d+),", block)],
            "clears": "WHERE `spelltrigger_1`=1;" in block,
            "inserted": "INSERT INTO `forever_item_template` SELECT * FROM `forever_item_stock`;" in block,
        }
    return items


def listed(text):
    return [] if text == "-" else [part.strip() for part in text.split(",")]


@pytest.fixture(scope="module")
def stock_rows():
    return [row for row in spec_rows("items.tsv") if row["kind"] == "stock"]


def test_each_changed_stock_item_has_its_forever_values_in_the_item_override_table(out, stock_rows):
    items = stock_items(out)
    # The changed stock items of the dungeon loot files (#242) are checked by their own tests.
    loot = {int(row["entry"]) for row in loot_item_rows() if row["kind"] == "stock"}
    assert sorted(set(items) - loot) == sorted(int(row["entry"]) for row in stock_rows)
    for spec in stock_rows:
        item = items[int(spec["entry"])]
        server = item["set"]
        assert item["inserted"] and spec["entry"] == spec["sourceItem"]
        assert server["name"] == spec["name"] and server["patch"] == "0"
        assert (int(server["quality"]), int(server["item_level"]), int(server["required_level"]),
                int(server["bonding"]), int(server["armor"]), int(server["display_id"]), int(server["sell_price"])) == \
            (QUALITIES[spec["quality"]], int(spec["itemLevel"]), int(spec["requiredLevel"]),
             BONDINGS[spec["bonding"]], int(spec["armor"]), int(spec["displayId"]), int(spec["sellPrice"])), spec["name"]
        stats = [s.split(" ") for s in listed(spec["stats"])]
        assert [(int(server["stat_type%d" % k]), int(server["stat_value%d" % k])) for k in range(1, 11)] == \
            [(STAT_TYPES[n], int(v)) for n, v in stats] + [(0, 0)] * (10 - len(stats))
        # "-" keeps the stock value: the column is not set.
        assert server.get("inventory_type") == (None if spec["inventoryType"] == "-" else str(SLOTS[spec["inventoryType"]]))
        assert server.get("delay") == (None if spec["delay"] == "-" else spec["delay"])
        damage = None if spec["damage"] == "-" else tuple(spec["damage"].split("-"))
        assert (None if "dmg_min1" not in server else (server["dmg_min1"], server["dmg_max1"])) == damage
        if spec["resistances"] == "-":
            assert not any("%s_res" % r in server for r in RESISTANCES)
        else:
            named = {n.lower(): v for n, v in (r.split(" ") for r in listed(spec["resistances"]) if r != "none")}
            assert {r: server["%s_res" % r] for r in RESISTANCES} == {r: named.get(r, "0") for r in RESISTANCES}
        # Neither description nor item set nor random property is touched on a stock item.
        assert not {"description", "set_id", "random_property"} & set(server)


def test_each_bonus_is_a_stock_equip_spell_that_gives_it(out, stock_rows):
    spells, _ = read_dbc(STOCK / "Spell.dbc")
    items = stock_items(out)
    for spec in stock_rows:
        item = items[int(spec["entry"])]
        if spec["bonuses"] == spec["equipSpells"] == "-":
            assert not item["clears"] and not item["equip"], spec["name"]  # the stock spells stay
            continue
        assert item["clears"]
        bonuses = [re.match(r"(\w+) (\d+)", b).groups() for b in listed(spec["bonuses"])]
        given = [bonus_of(spells, s) for s in item["equip"][:len(bonuses)]]
        assert given == [(n, int(v)) for n, v in bonuses], spec["name"]
        assert item["equip"][len(bonuses):] == [int(s) for s in listed(spec["equipSpells"])]


def test_each_rounded_bonus_keeps_its_forever_value_and_is_the_nearest_stock_value(stock_rows):
    spells, _ = read_dbc(STOCK / "Spell.dbc")
    stock_values = {}
    for spell, row in spells.items():
        # An equip-style spell: passive, no duration, no class family, no equipped item, spell level 0.
        if not (row[6] & 0x40 and row[30] == 21 and row[160] == 0 and row[58] == 0xFFFFFFFF and row[29] == 0):
            continue
        try:
            name, value = bonus_of(spells, spell)
        except AssertionError:
            continue
        stock_values.setdefault(name, set()).add(value)
    rounded = 0
    for spec in stock_rows:
        for bonus in listed(spec["bonuses"]):
            m = re.match(r"(\w+) (\d+) \(Forever ([\d.]+)\)$", bonus)
            if not m:
                continue
            rounded += 1
            name, value, forever = m.group(1), int(m.group(2)), float(m.group(3))
            # The nearest is at most as far as any other stock value; a tie goes up.
            assert all(abs(value - forever) <= abs(v - forever) for v in stock_values[name]), bonus
            assert not any(abs(v - forever) == abs(value - forever) and v > value for v in stock_values[name]), bonus
    assert rounded >= 1


def test_worked_examples_of_the_forever_values(out):
    items = stock_items(out)
    spells, _ = read_dbc(STOCK / "Spell.dbc")
    # Copper Mace (2844): Main Hand 2.2 -> One-Hand 2.6 (Forever client 2844).
    assert (items[2844]["set"]["inventory_type"], items[2844]["set"]["delay"]) == ("13", "2600")
    # Copper Shortsword (2847): white -> green, Str 1, bind on equip, 9-18 damage.
    assert {k: items[2847]["set"][k] for k in ("quality", "bonding", "stat_type1", "stat_value1", "dmg_min1", "dmg_max1")} == \
        {"quality": "2", "bonding": "2", "stat_type1": "4", "stat_value1": "1", "dmg_min1": "9", "dmg_max1": "18"}
    # Golden Scale Coif (3837): Str 10, Spi 10 -> Str 10, Sta 10.
    assert [(items[3837]["set"]["stat_type%d" % k], items[3837]["set"]["stat_value%d" % k]) for k in (1, 2, 3)] == \
        [("4", "10"), ("7", "10"), ("0", "0")]
    # Thorium Armor (12405) gains Stamina 15 and keeps its resistances.
    assert (items[12405]["set"]["stat_type1"], items[12405]["set"]["stat_value1"]) == ("7", "15")
    assert "fire_res" not in items[12405]["set"]
    # Jade Serpentblade (3850): Str 4 becomes AP 4 (9137, Attack Power 04).
    assert items[3850]["equip"] == [9137] and bonus_of(spells, 9137) == ("AP", 4)
    # Shadow Crescent Axe (3856): Forever spell power 10 has no stock spell; 9 and 11 do, and the tie goes up.
    assert items[3856]["equip"] == [9416] and bonus_of(spells, 9416) == ("SpellPower", 11)
    # Annihilator (12798): One-Hand, AP 14; its Armor Shatter chance on hit stays, as it is no on-equip spell.
    assert items[12798]["equip"] == [9142] and items[12798]["clears"]


def test_the_mithril_scale_pieces_are_mithril_plate_in_forever(out):
    # Spec #225 user story 7: the four "Mithril Scale" pieces are named "Mithril Plate" on the Forever realm
    # (the items here, #227; their recipes and plans in test_stock_recipes.py, #228).
    items = stock_items(out)
    assert {entry: items[entry]["set"]["name"] for entry in (7920, 7924, 7925, 7932)} == {
        7920: "Mithril Plate Pants", 7924: "Mithril Plate Bracers", 7925: "Mithril Plate Gloves",
        7932: "Mithril Plate Shoulders"}
    assert all(items[entry]["inserted"] for entry in (7920, 7924, 7925, 7932))


def test_the_spec_has_a_row_for_every_changed_readable_stock_crafted_item(stock_rows):
    if not (DUMP / "items.tsv").is_file():
        pytest.skip("no dump of the Forever client at %s" % DUMP)
    text = (Path(__file__).resolve().parents[3] / "forever" / "spec" / "items.tsv").read_text(encoding="utf-8")

    def header_ids(label):
        return {int(i) for i in re.search(r"^# %s[^:]*: ([\d, ]+)" % label, text, re.M).group(1).split(", ")}

    excluded = header_ids("Excluded")
    unchanged = header_ids("Unchanged in 1.12 terms")
    rows = {int(row["entry"]) for row in stock_rows}
    gear = ("quality", "ilvl", "reqlvl", "inv", "delay", "bonding", "stats", "armor", "dps", "name", "rating/aura", "spells")
    with open(DUMP / "items.tsv", encoding="utf-8") as handle:
        for item in csv.DictReader(handle, delimiter="\t"):
            if "creates:stock" not in item["used"]:
                continue
            entry = int(item["item"])
            if item["status"].startswith("not readable"):
                assert entry in excluded, item["name"]
                continue
            changed = {part.strip().split(" ")[0] for part in item["stock_diff"].split(";") if part.strip()}
            if item["cls"].split("/")[0] in ("2", "4") and changed & set(gear):
                assert entry in rows | unchanged, item["name"]
    assert excluded == {12779, 22762, 22763, 22764}
    assert len(rows) >= 100 and not rows & (excluded | unchanged)


def test_the_ticket_counts_of_changed_items(stock_rows):
    # #227: 23 white items became green, 39 new weapon speeds, 27 Main Hand weapons became One-Hand.
    notes = [row["notes"] for row in stock_rows]
    assert sum("quality 1->2" in n for n in notes) == 23
    assert sum(row["delay"] != "-" and "speed " in row["notes"] for row in stock_rows) == 39
    assert sum(row["inventoryType"] == "oneHand" and "slot 21->13" in row["notes"] for row in stock_rows) == 27


# --- Refusals: the generator fails and writes nothing.

def test_a_display_that_is_no_stock_display_is_refused(tmp_path):
    error = refused(tmp_path, "items.tsv", ("\t23528\t243\t", "\t999999\t243\t"))
    assert "999999 is no stock ItemDisplayInfo.dbc ID" in error


def test_a_bonus_with_no_stock_equip_spell_is_refused(tmp_path):
    error = refused(tmp_path, "items.tsv", ("\tSpellPower 12\t", "\tHaste 12\t"))
    assert "the bonus 'Haste 12' has no stock equip spell" in error


def test_a_bonus_value_with_no_stock_spell_and_no_forever_value_is_refused(tmp_path):
    error = refused(tmp_path, "items.tsv", ("\tSpellPower 11 (Forever 10)\t", "\tSpellPower 10\t"))
    assert "no stock equip spell gives SpellPower 10" in error


def test_a_rounded_value_that_is_not_the_nearest_is_refused(tmp_path):
    error = refused(tmp_path, "items.tsv", ("\tSpellPower 11 (Forever 10)\t", "\tSpellPower 9 (Forever 10)\t"))
    assert "SpellPower 10 rounds to 11, not 9" in error


def test_a_new_item_that_is_its_own_source_is_refused(tmp_path):
    # The #226 follow-up: a new item (kind item) must take a free item ID, even when it names itself as source.
    error = refused(tmp_path, "items.tsv", ("item\t70000\t3469\t", "item\t3469\t3469\t"))
    assert "outside the free item ID range 70000-79999" in error


def test_a_stock_item_that_is_not_its_own_source_is_refused(tmp_path):
    error = refused(tmp_path, "items.tsv", ("stock\t2844\t2844\t", "stock\t2844\t2845\t"))
    assert "a stock item is its own source" in error


def test_a_stock_item_in_the_free_item_range_is_refused(tmp_path):
    error = refused(tmp_path, "items.tsv", ("stock\t2844\t2844\t", "stock\t70500\t70500\t"))
    assert "a stock item has a stock ID, below the free item ID range 70000-79999" in error
