"""The rules that hold in every dungeon of spec #240 (#240 review: M1-M4, L1, L2, L5, M6; the lead data decisions).

The per-dungeon tests (test_loot_<dungeon>.py) hold the expected rows of one dungeon. This file holds the rules
over all spec files. Expected spell IDs come from the stock Spell.dbc and the stock item_template (the equip spell
that most stock items use for a value, equip_stats.py), not from the generator.
"""
import re

import pytest

from loot_helpers import (ITEM_FILES, LOOT_FILES, changed, item_block, item_rows, loot_list, new_equip_spells, sql,
                          stock_equip_spells)
from test_blacksmithing import spec_rows
from test_pipeline import STOCK

pytestmark = pytest.mark.skipif(not (STOCK / "Spell.dbc").is_file(), reason="no stock DBC folder at %s" % STOCK)

SOURCE = re.compile(r"(https://nether\.wowhead\.com/tooltip/\w+/\d+\?dataEnv=16&locale=0|https://www\.wowhead\.com/forever/\S+"
                    r"|client \d+\.\d+\.\d+\.\d+)$")


def all_item_rows():
    return [row for name in ITEM_FILES for row in spec_rows(name)]


# --- The spec files.

@pytest.mark.parametrize("name", ITEM_FILES + LOOT_FILES + ["loot_item_sets.tsv"])
def test_every_loot_row_names_its_source_and_date(name):
    rows = spec_rows(name)
    assert rows
    for row in rows:
        assert SOURCE.match(row["source"]), (row["name"], row["source"])
        assert re.fullmatch(r"\d{4}-\d{2}-\d{2}", row["date"]), row["name"]


@pytest.mark.parametrize("name", ITEM_FILES)
def test_the_new_items_of_a_file_take_their_ids_from_its_block(name):
    low, high = item_block(name)
    new = [int(r["entry"]) for r in spec_rows(name) if r["kind"] != "stock"]
    assert new and all(low <= e <= high for e in new)


# --- Rounding (spec story 12, lead decision: every stat with stock equip spells rounds to the nearest one).

# {entry: (name, the on-equip spells)}: the Forever stats that a stock equip spell gives once rounded.
ROUNDED = {
    71405: ("Twilight Lord Girdle", {23172}),            # +6 Block Value: Block Value 10
    10919: ("Apothecary Gloves", {9394, 13614}),          # Herbalism +2: 13614 (no tooltip text)
    6629: ("Sporid Cape", {21991}),                       # 1 Nature damage to the attacker: 4
    6463: ("Deep Fathom Ring", {27516}),                  # +2 Spell Penetration: 6
    71001: ("Bone Knuckles", {9138, 20877}),              # +6 Attack Power against Beasts: Beast Slaying 15
    5444: ("Miner's Cape", {9137, 22836, 13611}),         # +6 AP against Elementals: Elemental Slaying 36; Mining +2
    6314: ("Wolfmaster Cape", {9140, 20877}),             # +9 Attack Power against Beasts: Beast Slaying 15
    71511: ("Defias Jailbreakers", {9133}),               # Lockpicking +4: Lockpicking 5
    6910: ("Leech Pants", {9395, 21363, 21596}),          # 5 health per 5 sec: Vitality 5
    6743: ("Sustaining Ring", {21347}),                   # 4 health per 5 sec: Vitality 4
    71400: ("Spiked Shell Band", {21991}),                # 2 Nature damage to the attacker: 4
    71510: ("Nightskulker Ring", {24351}),                # Fear hit chance -4%: Fear Resistance 4
    71301: ("Sorcerer Collar", {18985}),                  # Silence hit chance -4%: Resist Silence 5
    6907: ("Tortoise Armor", {27518}),                    # Spell damage received -5: Spell Damage Reduction 10
}


def test_every_forever_stat_with_stock_equip_spells_is_rounded_to_the_nearest_one(out):
    text, new = sql(out), item_rows(out, "forever_item_new")
    got = {entry: set(new_equip_spells(new[entry])) if entry in new else set(stock_equip_spells(text, name, entry))
           for entry, (name, _) in ROUNDED.items()}
    assert got == {entry: spells for entry, (_, spells) in ROUNDED.items()}


def test_a_rounded_stat_is_no_longer_listed_as_left_out(out):
    listed = loot_list(out)
    assert "Block Value" not in listed.get(("left-out", "item", "71405"), "")
    assert ("left-out", "item", "71001") not in listed and ("left-out", "item", "6910") not in listed


# --- Shields (lead decision: every shield uses the `block` column).

def test_every_shield_with_a_forever_block_value_writes_it(out):
    rows = {**item_rows(out, "forever_item_stock"), **item_rows(out, "forever_item_new")}
    blocks = {}
    for row in all_item_rows():
        m = re.search(r"(\d+) [Bb]lock(?! Value)", row["foreverStats"])
        if m and row["status"] == "confirmed":
            blocks[int(row["entry"])] = m.group(1)
    # The six shields of the dungeons (Wowhead Forever tooltips).
    assert blocks == {6447: "9", 13245: "10", 6630: "11", 7002: "13", 5443: "10", 6320: "12", 71507: "14"}
    assert {entry: rows[entry].get("block") for entry in blocks} == blocks


# --- Unique-Equipped (lead decision: no 1.12 meaning, left off and listed).

# The items whose Forever tooltip says Unique-Equipped (notes/forever/wowhead-cache/item).
UNIQUE_EQUIPPED = {71400, 71401, 5192, 71203, 71204, 6321, 6220, 71303, 71502, 71510, 6463}


def test_unique_equipped_is_listed_as_left_out_on_every_item_that_has_it(out):
    listed = loot_list(out)
    assert {int(key[2]) for key, detail in listed.items()
            if key[0] == "left-out" and "Unique-Equipped (no 1.12 meaning)" in detail} == UNIQUE_EQUIPPED


def test_no_loot_item_sets_its_max_count(out):
    # A new item keeps the max_count of the stock item it copies: max_count 1 would be stricter than Unique-Equipped.
    rows = {**item_rows(out, "forever_item_stock"), **item_rows(out, "forever_item_new")}
    assert not [e for e, r in rows.items() if "max_count" in r]


# --- Lists.

def test_the_camp_blueprints_are_listed_as_left_out_with_their_boss_and_chance(out):
    listed = loot_list(out)
    blueprints = {int(key[2]): detail for key, detail in listed.items() if key[:2] == ("left-out", "drop")}
    assert set(blueprints) == {273087, 273098, 273086, 273102, 273096, 273105, 273141}
    assert all(re.search(r"boss \d+ .*, chance [\d.]+%", detail) for detail in blueprints.values()), blueprints
    assert "boss 6243 Gelihast, chance 1.09%" in blueprints[273141]


def test_silvered_gauntlets_are_unconfirmed_and_stay_out(out):
    assert ("unconfirmed", "item", "71407") in loot_list(out)
    assert 71407 not in item_rows(out, "forever_item_new")


def test_every_source_that_a_script_cannot_read_is_listed(out):
    # Spec story 23: wowhead.com pages answer 403 to scripts (a web-fetch tool reads them), so a refresh places
    # these rows by hand. The tooltip service and the client build are read by script.
    lines = [line.split("\t") for line in (out / "loot-list.tsv").read_text(encoding="utf-8").splitlines()[2:]]
    manual = {(kind, ident, detail) for listed, kind, ident, _, detail in lines if listed == "manual-source"}
    expected = {("item" if name in ITEM_FILES else r["kind"], r["entry"] if name in ITEM_FILES else r["item"], r["source"])
                for name in ITEM_FILES + LOOT_FILES for r in spec_rows(name)
                if r["status"] == "confirmed" and r["source"].startswith("https://www.wowhead.com/")}
    assert len(expected) > 20 and manual == expected


def test_a_confirmed_new_item_that_no_confirmed_drop_or_reward_names_stays_out_and_is_listed(tmp_path):
    # Spec story 22: the item row is confirmed, but nothing confirms where it comes from (as 71407 before the lead
    # decision). Fallenroot Longbow (71404) loses its only drop row's confirmation.
    out, done = changed(tmp_path, ("loot_blackfathom_deeps.tsv", "9.96\t-\t-\tconfirmed", "9.96\t-\t-\tunconfirmed"))
    assert done.returncode == 0, done.stderr
    listed = loot_list(out)
    assert "no confirmed drop, reward or item set names it" in listed[("unconfirmed", "item", "71404")]
    assert 71404 not in item_rows(out, "forever_item_new")
