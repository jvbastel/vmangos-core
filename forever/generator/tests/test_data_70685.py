"""The numbers of client build 1.60.1.70685 in the generated data (the player's decision for spec #204, the
audit notes/forever/spec-vs-70685.md of the lead's notes).

The seam is the generator output, read from the outside: the client Spell.dbc and the server SQL. The
expected numbers are the client values of build 1.60.1.70685 (its Spell, SpellEffect, SpellCooldowns and
SpellEquippedItems rows), written here as literals.

It needs the stock 1.12.1 DBC files (FOREVER_STOCK_DBC). Without them it skips.
"""
import re
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


BATTLE_SHOUT = (6673, 5242, 6192, 11549, 11550, 11551, 25289)
DEMORALIZING_SHOUT = (1160, 6190, 11554, 11555, 11556)
SPELL_EFFECT_THREAT = 63


def test_battle_shout_gains_the_client_attack_power_per_level(client):
    assert [per_level(client[s]) for s in BATTLE_SHOUT] == [0.3, 0.3, 0.3, 0.6, 0.6, 0.6, 0.6]


def test_demoralizing_shout_loses_the_client_attack_power_per_level(client):
    assert [per_level(client[s]) for s in DEMORALIZING_SHOUT] == [-1.4] * 5


def test_demoralizing_shout_threat_grows_per_level_on_every_target_hit(client):
    # Client: effect 2 THREAT 11/19/27/35/43 + 0.8 per level, on the targets of effect 1. The flat part stays
    # in the stock spell_threat rows (the same 11/19/27/35/43), so effect 2 adds only the per-level part.
    for spell in DEMORALIZING_SHOUT:
        row = client[spell]
        assert row[EFFECT + 1] == SPELL_EFFECT_THREAT
        assert row[TARGET_A + 1] == row[TARGET_A]
        assert int32(row[BASE_POINTS + 1]) + 1 == 0
        assert per_level(row, 1) == 0.8


SUNDER_ARMOR = (7386, 7405, 8380, 11596, 11597)


def server_threat(out):
    """entry -> flat threat of the generated forever_spell_threat rows."""
    sql = (out / "sql" / "forever_world.sql").read_text(encoding="utf-8")
    assert "CREATE TABLE `forever_spell_threat` LIKE `spell_threat`;" in sql
    rows = re.findall(r"^INSERT INTO `forever_spell_threat` \(`entry`, `threat`\) VALUES (.*);$", sql, re.M)
    return {int(e): float(t) for line in rows for e, t in re.findall(r"\((\d+), (-?[\d.]+)\)", line)}


def test_sunder_armor_has_the_client_threat(out):
    # Client: effect 2 THREAT 34/75/117/158/206 (stock spell_threat 45/99/153/207/261), no attack power part.
    threat = server_threat(out)
    assert [threat[s] for s in SUNDER_ARMOR] == [34, 75, 117, 158, 206]
