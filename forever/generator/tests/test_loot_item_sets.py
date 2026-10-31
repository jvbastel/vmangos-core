"""The Forever item sets with a piece in the six dungeons of spec #240 (jvbastel/WOWAI#248).

The spec rows are in forever/spec/loot_item_sets.tsv. The test runs the generator on the spec folder and reads its
outputs from the outside: ItemSet.dbc of the Forever DBC folder (the server reads it there with Forever.Rules = 1)
and of the client data patch, the set_id of the pieces in forever_world.sql, and the loot list.

The expected values are the Forever client data (build 1.60.1.70685, ItemSet and ItemSetSpell 161, 162 and 2131,
the SpellEffect rows of each bonus spell), each Forever bonus as the stock 1.12 equip spell of the same aura and
value. The stock values are the stock 1.12 ItemSet.dbc. A set has a piece in the six dungeons when a piece is an
item row of a loot_*_items.tsv file or a stock drop or quest reward of the six dungeons (stock world database,
read 2026-10-31): only the stock sets 161 and 162 and the new Forever set 2131 have one.
"""
import pytest

from test_item_sets import item_set, set_ids
from test_loot_rfc import changed, loot_list, refused  # noqa: F401
from test_pipeline import STOCK, generate, read_dbc

pytestmark = pytest.mark.skipif(not (STOCK / "Spell.dbc").is_file(), reason="no stock DBC folder at %s" % STOCK)

SETS = "loot_item_sets.tsv"
DEFIAS, VIPER, PARTNERS = 161, 162, 2131
EXPECTED = {
    DEFIAS: {
        "name": "Defias Leather",
        # The Deadmines pieces, as in stock: Armor, Belt, Boots, Gloves, Leggings.
        "items": {10399, 10403, 10402, 10401, 10400},
        "bonuses": {
            (14707, 2),   # Forever 14707: +5 Arcane Resistance (the stock spell itself; stock 3 pieces)
            # 3: Forever 1292025 +15 Attack Power against Humanoids: no stock spell (no stock Humanoid slaying).
            # 4: Forever 1292028 Devious Strike (a bleed proc from behind): no stock spell.
            (7534, 5),    # Forever 7534: Daggers +1 (the stock spell itself; stock 4 pieces)
        },
    },
    VIPER: {
        "name": "Embrace of the Viper",
        # The Wailing Caverns pieces, as in stock: Belt, Footpads, Gloves, Leggings, Armor.
        "items": {10412, 10411, 10413, 10410, 6473},
        "bonuses": {
            (9106, 2),    # Forever 9106: +10 Intellect (the stock spell itself; stock 5 pieces)
            (9140, 3),    # Forever 9140: Attack Power 10 (the stock spell itself)
            # 4: Forever 1291796 Embrace of the Viper (a heal over time at low health): no stock spell.
            # 5: Forever 1291807 Dream Venom (a stun proc): no stock spell.
        },
    },
}
# The stock bonuses that Forever replaced (stock ItemSet.dbc): Defias 2 Armor 10 (9760), Viper 2 Nature damage 7
# (7694), 3 Staves +2 (20607), 4 Healing 11 (7679).
STOCK_ONLY = {DEFIAS: {(9760, 2), (14707, 3), (7534, 4), (9140, 5)},
              VIPER: {(7694, 2), (20607, 3), (7679, 4), (9106, 5)}}
# Each left-out set bonus (loot list name) and a part of its Forever value (loot list detail).
LEFT_OUT = {
    (DEFIAS, "Defias Leather (3)"): "1292025",
    (DEFIAS, "Defias Leather (4)"): "1292028",
    (VIPER, "Embrace of the Viper (4)"): "1291796",
    (VIPER, "Embrace of the Viper (5)"): "1291807",
    (PARTNERS, "Partners in Crime (2)"): "1317038",
    (PARTNERS, "Partners in Crime (2, Angry)"): "1317039",
}


@pytest.fixture(scope="module")
def out(tmp_path_factory):
    return generate(tmp_path_factory.mktemp("forever248"))


@pytest.mark.parametrize("set_id", [DEFIAS, VIPER])
def test_the_server_item_set_table_has_the_forever_bonuses_as_stock_equip_spells(out, set_id):
    rows, text = read_dbc(out / "dbc" / "ItemSet.dbc")
    found = item_set(rows[set_id], text)
    assert found == EXPECTED[set_id]
    assert not (found["bonuses"] & STOCK_ONLY[set_id])


def test_the_client_data_patch_has_the_same_item_set_table(out):
    assert (out / "client" / "DBFilesClient" / "ItemSet.dbc").read_bytes() == (out / "dbc" / "ItemSet.dbc").read_bytes()


def test_partners_in_crime_is_left_out_of_both_tables(out):
    # Its other piece (Forever 284716, an epic) has no source, so one piece cannot reach the 2-piece bonuses.
    for path in (out / "dbc" / "ItemSet.dbc", out / "client" / "DBFilesClient" / "ItemSet.dbc"):
        rows, _ = read_dbc(path)
        assert PARTNERS not in rows
    assert 71204 not in set_ids(out)


def test_the_dungeon_set_1_sets_stay_stock(out):
    rows, text = read_dbc(out / "dbc" / "ItemSet.dbc")
    stock, stock_text = read_dbc(STOCK / "ItemSet.dbc")
    for set_id in range(181, 190):
        assert rows[set_id][:1] + rows[set_id][2:] == stock[set_id][:1] + stock[set_id][2:]
        assert text(rows[set_id][1]) == stock_text(stock[set_id][1])


def test_the_dungeon_pieces_with_forever_stats_keep_their_set_on_the_server(out):
    found = set_ids(out)
    assert {e: found.get(e) for e in (10399, 10403)} == {10399: DEFIAS, 10403: DEFIAS}
    assert {e: found.get(e) for e in (6473, 10410, 10411, 10412)} == dict.fromkeys((6473, 10410, 10411, 10412), VIPER)


def set_lines(out):
    """{(set ID, name): detail} of the left-out set rows of the loot list."""
    lines = (out / "loot-list.tsv").read_text(encoding="utf-8").splitlines()[2:]
    return {(int(f[2]), f[3]): f[4] for f in (line.split("\t") for line in lines) if f[1] in ("set", "set bonus")}


def test_the_loot_list_names_every_left_out_set_bonus_with_its_forever_value(out):
    found = set_lines(out)
    bonuses = {key: detail for key, detail in found.items() if key[1].endswith(")")}
    assert set(bonuses) == set(LEFT_OUT)
    for key, spell in LEFT_OUT.items():
        assert "Forever %s" % spell in bonuses[key]
    assert "Attack Power against Humanoids" in bonuses[(DEFIAS, "Defias Leather (3)")]
    assert (PARTNERS, "Partners in Crime") in found


# --- Refusals: the generator stops with an error and writes nothing.

def test_a_set_row_with_no_source_is_refused(tmp_path):
    error = refused(tmp_path, (SETS, "Defias Leather\t-\t10399,10403,10402,10401,10400\t-\t-\t-\tclient 1.60.1.70685",
                               "Defias Leather\t-\t10399,10403,10402,10401,10400\t-\t-\t-\t-"))
    assert "item set row" in error and "has no source" in error


def test_a_bonus_row_with_no_date_is_refused(tmp_path):
    error = refused(tmp_path, (SETS, "\t14707\t-\tclient 1.60.1.70685\t2026-10-31", "\t14707\t-\tclient 1.60.1.70685\t-"))
    assert "the date '-' is not YYYY-MM-DD" in error


def test_a_bonus_left_off_without_its_forever_value_is_refused(tmp_path):
    error = refused(tmp_path, (SETS, "\tnone\t1292028\tForever 1292028 Devious Strike: 5% chance on a melee hit from behind to make "
                               "the target bleed for 15 Physical damage every 1 sec for 5 sec (1292029)\t", "\tnone\t1292028\t-\t"))
    assert "Defias Leather (4)" in error and "left off" in error and "leftOut" in error


def test_a_set_id_in_two_spec_files_is_refused(tmp_path):
    # Imperial Plate (321) is a set of item_sets.tsv (#231).
    error = refused(tmp_path, extra=[(SETS, "\t".join(["stockset", "321", "Imperial Plate", "-", "12424", "-", "-", "-",
                                                         "client 1.60.1.70685", "2026-10-31", "test"]))])
    assert "item set 321 is in the spec twice" in error
