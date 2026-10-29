"""The numbers of client build 1.60.1.70685 in the generated data (the player's decision for spec #204, the
audit notes/forever/spec-vs-70685.md of the lead's notes).

The seam is the generator output, read from the outside: the client Spell.dbc and the server SQL. The
expected numbers are the client values of build 1.60.1.70685 (its Spell, SpellEffect, SpellCooldowns and
SpellEquippedItems rows), written here as literals.

It needs the stock 1.12.1 DBC files (FOREVER_STOCK_DBC). Without them it skips.
"""
import struct

import pytest

from test_pipeline import STOCK, generate, int32, read_dbc, server_spells

pytestmark = pytest.mark.skipif(not (STOCK / "Spell.dbc").is_file(), reason="no stock DBC folder at %s" % STOCK)

# Spell.dbc 1.12.1 fields.
EQUIPPED_ITEM_CLASS, EQUIPPED_ITEM_SUBCLASS_MASK = 58, 59
EFFECT, POINTS_PER_LEVEL, BASE_POINTS, TARGET_A = 61, 73, 76, 82
START_RECOVERY_CATEGORY, START_RECOVERY_TIME = 157, 158
GLOBAL_COOLDOWN_CATEGORY = 133

VICTORY_RUSH = 60123


@pytest.fixture(scope="module")
def out(tmp_path_factory):
    return generate(tmp_path_factory.mktemp("forever70685"))


@pytest.fixture(scope="module")
def client(out):
    return read_dbc(out / "client" / "DBFilesClient" / "Spell.dbc")[0]


@pytest.fixture(scope="module")
def server(out):
    return server_spells(out)


def per_level(row, effect=0):
    return round(struct.unpack("<f", struct.pack("<I", row[POINTS_PER_LEVEL + effect]))[0], 4)


def test_victory_rush_has_no_damage_per_level(client, server):
    # Client 402927: 1 damage at every level (the source spell 12170 has 0.9 per level).
    assert [per_level(client[VICTORY_RUSH], k) for k in range(3)] == [0, 0, 0]
    assert float(server[VICTORY_RUSH]["effectRealPointsPerLevel1"]) == 0


def test_victory_rush_is_on_the_global_cooldown(client, server):
    row = client[VICTORY_RUSH]
    assert (row[START_RECOVERY_CATEGORY], row[START_RECOVERY_TIME]) == (GLOBAL_COOLDOWN_CATEGORY, 1500)
    assert (int(server[VICTORY_RUSH]["startRecoveryCategory"]), int(server[VICTORY_RUSH]["startRecoveryTime"])) == \
        (GLOBAL_COOLDOWN_CATEGORY, 1500)


def test_victory_rush_needs_no_weapon(client, server):
    row = client[VICTORY_RUSH]
    assert (int32(row[EQUIPPED_ITEM_CLASS]), row[EQUIPPED_ITEM_SUBCLASS_MASK]) == (-1, 0)
    assert int(server[VICTORY_RUSH]["equippedItemClass"]) == -1
