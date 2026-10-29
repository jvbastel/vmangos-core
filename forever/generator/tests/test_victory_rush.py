"""Victory Rush and Victorious under the Forever rules (jvbastel/WOWAI#213) in the generated data.

Victory Rush needs a caster aura state that the core sets while the Warrior has the Victorious aura
(20 sec after a kill that yields experience or honor). The client checks the same aura state in its own
Spell.dbc row against UNIT_FIELD_AURASTATE, so the server row and the client row must name the same
state, and no stock spell may use it.
"""
import re

import pytest

from test_pipeline import ROOT, STOCK, generate, read_dbc, server_spells

pytestmark = pytest.mark.skipif(not (STOCK / "Spell.dbc").is_file(), reason="no stock DBC folder at %s" % STOCK)

VICTORY_RUSH, VICTORIOUS = 60123, 60124
# The free aura state that the core and the client data patch use for Victorious (SpellDefines.h: 6 is "not used").
VICTORIOUS_STATE = 6
# Spell.dbc 1.12.1 fields.
CASTER_AURA_STATE, TARGET_AURA_STATE, RECOVERY_TIME, DURATION_INDEX = 16, 17, 19, 30
EFFECT, BASE_POINTS, APPLY_AURA_NAME = 61, 76, 91
SCHOOL_DAMAGE, DUMMY, APPLY_AURA, AURA_DUMMY = 2, 3, 6, 4
DURATION_20_SEC = 18  # SpellDuration.dbc: 20000 ms
RULES = ROOT / "src" / "game" / "Forever" / "VictoryRushRules.h"
IDS = ROOT / "src" / "game" / "Forever" / "ForeverSpellIds.h"


@pytest.fixture(scope="module")
def out(tmp_path_factory):
    return generate(tmp_path_factory.mktemp("forever213"))


def client_spells(out):
    rows, _ = read_dbc(out / "client" / "DBFilesClient" / "Spell.dbc")
    return rows


def test_no_stock_spell_uses_the_victorious_aura_state():
    stock, _ = read_dbc(STOCK / "Spell.dbc")
    users = [i for i, row in stock.items() if VICTORIOUS_STATE in (row[CASTER_AURA_STATE], row[TARGET_AURA_STATE])]
    assert users == []


def test_victory_rush_needs_the_victorious_aura_state_in_the_client_and_the_server(out):
    assert client_spells(out)[VICTORY_RUSH][CASTER_AURA_STATE] == VICTORIOUS_STATE
    assert int(server_spells(out)[VICTORY_RUSH]["casterAuraState"]) == VICTORIOUS_STATE


def test_the_core_sets_the_same_aura_state_for_the_same_spells():
    text = RULES.read_text(encoding="utf-8")
    assert re.search(r"\bVictoriousAuraState = %d;" % VICTORIOUS_STATE, text)
    ids = IDS.read_text(encoding="utf-8")
    assert re.search(r"\bVictoryRushSpell = %d;" % VICTORY_RUSH, ids)
    assert re.search(r"\bVictoriousSpell = %d;" % VICTORIOUS, ids)


def test_victory_rush_has_the_client_numbers(out):
    # Client 402927: 30 sec cooldown; damage 1 + 15% of attack power (effect 3), heals 10% (effect 2).
    row = client_spells(out)[VICTORY_RUSH]
    assert row[RECOVERY_TIME] == 30000
    assert row[EFFECT:EFFECT + 3] == (SCHOOL_DAMAGE, DUMMY, DUMMY)
    assert [row[BASE_POINTS + i] + 1 for i in range(3)] == [1, 10, 15]


def test_victorious_is_a_20_sec_dummy_aura(out):
    # Client 402975: a 20 sec DUMMY aura. The core sets and clears the aura state with it.
    row = client_spells(out)[VICTORIOUS]
    assert row[EFFECT] == APPLY_AURA and row[APPLY_AURA_NAME] == AURA_DUMMY
    assert row[DURATION_INDEX] == DURATION_20_SEC
