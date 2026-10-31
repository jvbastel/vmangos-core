"""The Forever item sets of jvbastel/WOWAI#231 (spec #225): the new Blessed Plate set and the stock Imperial
Plate set with its eighth piece and rewritten bonuses.

The real-spec tests run the generator on forever/spec and read its outputs from the outside: ItemSet.dbc of
the Forever DBC folder (the server reads it there with Forever.Rules = 1) and of the client data patch, and
the set_id of the pieces in forever_world.sql. The expected values are the Forever client data (build
1.60.1.70685: ItemSet 1968 and 321 and their ItemSetSpell rows), each Forever bonus as the stock 1.12 equip
spell of the same aura and value. The refusal tests change one thing and expect the generator to fail and
write nothing.
"""
import re

import pytest

from test_new_recipes import refused, run, spec_folder
from test_pipeline import STOCK, pytestmark, read_dbc  # noqa: F401  (pytestmark: skip without stock data)

# ItemSet.dbc 1.12.1: 0 ID, 1-8 Name, 9 the name flags, 10-26 ItemID[17], 27-34 SetSpellID[8],
# 35-42 SetThreshold[8], 43 RequiredSkill, 44 RequiredSkillRank.
NAME, ITEMS, SPELLS, THRESHOLDS = 1, 10, 27, 35
MAX_PIECES, MAX_SET_SPELLS = 17, 8

BLESSED, IMPERIAL = 1968, 321
EXPECTED = {
    BLESSED: {
        "name": "Blessed Plate",
        # Forever items 250586-250594 (no 250589, an Imperial piece): the #230 items 70202-70218.
        "items": {70202, 70204, 70206, 70210, 70212, 70214, 70216, 70218},
        "bonuses": {
            (14673, 2),               # Forever 14673: +10 Shadow Resistance (the stock spell itself)
            (14492, 3),               # Forever 1252009 +20 Strength: Increased Strength 20
            (23210, 4),               # Forever 1302375 "Blessed" 20 health per 5 sec to the party: Vitality 16
            (15465, 5), (23729, 5),   # Forever 1252013 2% hit (melee and spell): Hit 2, SpellHit 2
            (21518, 6),               # Forever 21518: Holy damage 29 (the stock spell itself)
        },
    },
    IMPERIAL: {
        "name": "Imperial Plate",
        # The 7 stock pieces and Imperial Plate Gauntlets (Forever 250589, #230 item 70208).
        "items": {12422, 12424, 12425, 12426, 12427, 12428, 12429, 70208},
        "bonuses": {
            (13385, 2),               # Forever 13385: Defense 7 (the stock spell itself)
            (15464, 3), (23727, 3),   # Forever 1251990 1% hit (melee and spell): Hit 1, SpellHit 1
            # Forever 1302372 "Imperial March" (4 pieces: movement speed not below 80%) has no 1.12 aura.
            (14492, 5),               # Forever 1251984 +20 Strength: Increased Strength 20
            (7597, 6), (18384, 6),    # Forever 1251991 1% crit (melee and spell): Crit 1, SpellCrit 1
        },
    },
}


def item_set(row, text):
    return {
        "name": text(row[NAME]),
        "items": {i for i in row[ITEMS:ITEMS + MAX_PIECES] if i},
        "bonuses": {(s, t) for s, t in zip(row[SPELLS:SPELLS + MAX_SET_SPELLS], row[THRESHOLDS:THRESHOLDS + MAX_SET_SPELLS]) if s},
    }


@pytest.mark.parametrize("set_id", [BLESSED, IMPERIAL])
def test_the_server_item_set_table_has_the_set_with_its_pieces_and_stock_equip_spell_bonuses(out, set_id):
    rows, text = read_dbc(out / "dbc" / "ItemSet.dbc")
    assert item_set(rows[set_id], text) == EXPECTED[set_id]


def test_the_client_data_patch_has_the_same_item_set_table(out):
    assert (out / "client" / "DBFilesClient" / "ItemSet.dbc").read_bytes() == (out / "dbc" / "ItemSet.dbc").read_bytes()


def test_the_other_stock_item_sets_stay_byte_for_byte(out):
    rows, text = read_dbc(out / "dbc" / "ItemSet.dbc")
    stock, stock_text = read_dbc(STOCK / "ItemSet.dbc")
    assert set(rows) == set(stock) | {BLESSED}
    for set_id, row in stock.items():
        # The dungeon sets 161 and 162 of #248 change too (test_loot_item_sets.py).
        if set_id not in (IMPERIAL, 161, 162):
            assert row[:1] + row[2:] == rows[set_id][:1] + rows[set_id][2:]
            assert text(rows[set_id][NAME]) == stock_text(row[NAME])


def set_ids(out):
    """{item entry: set_id} of the set_id UPDATE lines of forever_world.sql."""
    sql = (out / "sql" / "forever_world.sql").read_text(encoding="utf-8")
    found = {}
    for set_id, entries in re.findall(r"^UPDATE `forever_item_template` SET `set_id`=(\d+) WHERE `entry` IN \(([\d, ]+)\);$",
                                       sql, re.M):
        found.update({int(e): int(set_id) for e in entries.split(",")})
    return found


def test_the_server_item_rows_of_the_new_pieces_carry_their_set(out):
    # The stock Imperial pieces keep their stock rows (set_id 321 in the world database).
    found = {entry: set_id for entry, set_id in set_ids(out).items() if set_id in (BLESSED, IMPERIAL)}
    assert found == {**{entry: BLESSED for entry in EXPECTED[BLESSED]["items"]}, 70208: IMPERIAL}


def test_a_new_item_copied_from_a_set_piece_is_in_no_set(out):
    # Officer's Waistguard (70172) copies Imperial Plate Belt (12424) and is no piece: its copy says set_id 0.
    sql = (out / "sql" / "forever_world.sql").read_text(encoding="utf-8")
    m = re.search(r"^-- Item Officer's Waistguard \(70172\).*\n.*\n.*\nUPDATE `forever_item_new` SET (.*);$", sql, re.M)
    assert m and "`set_id`=0" in m.group(1)
    assert 70172 not in set_ids(out)


# --- Refusals: the generator stops with an error and writes nothing.

BLESSED_PIECES = "70218,70216,70214,70212,70210,70206,70204,70202"


def test_a_piece_that_is_no_item_row_of_the_spec(tmp_path):
    error = refused(tmp_path, changes=[("item_sets.tsv", BLESSED_PIECES, BLESSED_PIECES + ",70999")])
    assert "item 70999 is no item row of the spec" in error


def test_a_stock_piece_of_another_stock_set(tmp_path):
    # 11729 is a stock piece of The Gladiator (1), not of Blessed Plate, and no item row of the spec.
    error = refused(tmp_path, changes=[("item_sets.tsv", BLESSED_PIECES, BLESSED_PIECES + ",11729")])
    assert "item 11729 is no item row of the spec (or the dead-content rule cut it) and no stock piece of this set" in error


def test_a_piece_in_two_sets(tmp_path):
    # Imperial Plate Belt (12424) is a piece of Imperial Plate (321) already.
    error = refused(tmp_path, changes=[("item_sets.tsv", BLESSED_PIECES, BLESSED_PIECES + ",12424")])
    assert "item set Blessed Plate (1968): item 12424 is a piece of item set 321 too" in error


def test_a_bonus_with_no_stock_equip_spell(tmp_path):
    error = refused(tmp_path, changes=[("item_sets.tsv", "Strength 20\t1252009", "Strength 21\t1252009")])
    assert "no stock equip spell gives Strength 21" in error


def test_a_threshold_above_the_number_of_pieces(tmp_path):
    error = refused(tmp_path, changes=[("item_sets.tsv", "Blessed Plate (6)\t6", "Blessed Plate (6)\t9")])
    assert "the threshold 9 is not 1 to 8" in error


def test_more_than_8_bonus_spells(tmp_path):
    error = refused(tmp_path, changes=[("item_sets.tsv", "HolyDamage 29\t", "HolyDamage 29, Crit 1, SpellCrit 1, Hit 1\t")])
    assert "9 bonus spells, at most 8" in error


def test_a_bonus_of_a_set_with_no_set_row(tmp_path):
    error = refused(tmp_path, changes=[("item_sets.tsv", "bonus\t1968\tBlessed Plate (2)", "bonus\t1969\tBlessed Plate (2)")])
    assert "a bonus of item set 1969, which has no set row" in error


def test_a_piece_that_the_dead_content_rule_cuts(tmp_path):
    # Blessed Plate Pauldrons (70202) made at a Master Forge: the rule cuts the recipe and the item.
    done = refused(tmp_path, changes=[("blacksmithing.tsv", "70202x1\tAnvil", "70202x1\tMaster Forge")])
    assert "item 70202 is no item row of the spec (or the dead-content rule cut it)" in done


def test_a_new_set_on_a_stock_set_id(tmp_path):
    # A new set (kind set) whose ID is a stock set ID would rewrite that stock set: an ID clash (US35).
    error = refused(tmp_path, changes=[("item_sets.tsv", "set\t1968\tBlessed Plate", "set\t321\tBlessed Plate"),
                                       ("item_sets.tsv", "stockset\t321\tImperial Plate", "stockset\t1968\tImperial Plate")])
    assert "item set Blessed Plate (321): a new set (kind set) has the ID of a stock set" in error


def test_a_changed_stock_set_that_is_no_stock_set(tmp_path):
    error = refused(tmp_path, changes=[("item_sets.tsv", "set\t1968\tBlessed Plate", "stockset\t1968\tBlessed Plate")])
    assert "item set Blessed Plate (1968): a changed stock set (kind stockset) names no stock set" in error
