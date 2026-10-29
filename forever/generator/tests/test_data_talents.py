"""The data-only talents of the Forever rules (jvbastel/WOWAI#211): their generated rows give their effect
with 1.12 spell effects and auras.

The seam is the generator output, read from the outside: the client Spell.dbc (the same values as the
server copies) and the server SQL. A 1.12 spell modifier (aura 107/108) affects a spell when both are of
the Warrior spell family and the modifier's effect item type (its mask) shares a bit with the spell's family
flags. So the tests ask which Warrior abilities a talent's modifier reaches, by name. The expected names
come from the client data of build 1.60.1.70685 (its family masks) and the talent tooltips.

It needs the stock 1.12.1 DBC files (FOREVER_STOCK_DBC). Without them it skips.
"""
import re

import pytest

from test_pipeline import STOCK, generate, int32, read_dbc

pytestmark = pytest.mark.skipif(not (STOCK / "Spell.dbc").is_file(), reason="no stock DBC folder at %s" % STOCK)

# Spell.dbc 1.12.1 fields.
EFFECT, AURA, BASE_POINTS, MISC, ITEM_TYPE = 61, 91, 76, 106, 103
PROC_FLAGS, PROC_CHANCE, PROC_CHARGES = 24, 25, 26
FAMILY_NAME, FAMILY_FLAGS = 160, 161
NAME = 120
SPELLFAMILY_WARRIOR = 4
ADD_FLAT_MODIFIER, ADD_PCT_MODIFIER = 107, 108
SPELLMOD_ALL_EFFECTS, SPELLMOD_COST = 8, 14

# The Warrior abilities a player can learn in 1.12, one rank each (all ranks share the family flags).
ABILITIES = {
    "Charge": 100, "Hamstring": 1715, "Overpower": 7384, "Pummel": 6552, "Recklessness": 1719, "Rend": 772,
    "Heroic Strike": 78, "Thunder Clap": 6343, "Bloodrage": 2687, "Disarm": 676, "Revenge": 6572,
    "Shield Bash": 72, "Shield Block": 2565, "Shield Wall": 871, "Sunder Armor": 7386, "Taunt": 355,
    "Battle Shout": 6673, "Demoralizing Shout": 1160, "Intimidating Shout": 5246, "Challenging Shout": 1161,
    "Slam": 1464, "Cleave": 845, "Mortal Strike": 12294, "Bloodthirst": 23881, "Shield Slam": 23922,
    "Concussion Blow": 12809, "Mocking Blow": 694, "Berserker Rage": 18499, "Execute": 5308,
    "Intercept": 20252, "Whirlwind": 1680, "Retaliation": 20230, "Piercing Howl": 12323,
    "Sweeping Strikes": 12292, "Death Wish": 12328, "Last Stand": 12975,
    # The trigger spell of Bloodrage: its Rage over time.
    "Bloodrage (Rage over time)": 29131,
}
# Pummel's second rank has no family flags in the stock data either.
PUMMEL_RANKS = (6552, 6554)


@pytest.fixture(scope="module")
def out(tmp_path_factory):
    return generate(tmp_path_factory.mktemp("forever"))


@pytest.fixture(scope="module")
def client(out):
    return read_dbc(out / "client" / "DBFilesClient" / "Spell.dbc")[0]


def reached(client, talent_spell, effect=0):
    """The names of the Warrior abilities that a spell modifier effect of a talent rank affects."""
    talent = client[talent_spell]
    assert talent[AURA + effect] in (ADD_FLAT_MODIFIER, ADD_PCT_MODIFIER), talent_spell
    mask = talent[ITEM_TYPE + effect]
    return {name for name, spell in ABILITIES.items()
            if client[spell][FAMILY_NAME] == SPELLFAMILY_WARRIOR and client[spell][FAMILY_FLAGS] & mask}


def modifier(client, spell, effect=0):
    row = client[spell]
    return row[AURA + effect], row[MISC + effect], int32(row[BASE_POINTS + effect]) + 1


def test_focused_rage_reduces_the_rage_cost_of_the_offensive_abilities(client):
    # Client 70685, talent spell 29787: flat -1/-2/-3 Rage on the family mask 0x6E6E4EEE 0x00102224. That
    # takes the shouts other than Battle Shout and Piercing Howl too; Whirlwind is in the second word.
    assert [modifier(client, s) for s in (60120, 60121, 60122)] == [
        (ADD_FLAT_MODIFIER, SPELLMOD_COST, -10), (ADD_FLAT_MODIFIER, SPELLMOD_COST, -20),
        (ADD_FLAT_MODIFIER, SPELLMOD_COST, -30)]
    assert reached(client, 60120) - {"Retaliation"} == {
        "Hamstring", "Overpower", "Pummel", "Rend", "Heroic Strike", "Thunder Clap", "Disarm", "Revenge",
        "Shield Bash", "Sunder Armor", "Demoralizing Shout", "Intimidating Shout", "Challenging Shout", "Slam",
        "Cleave", "Mortal Strike", "Bloodthirst", "Shield Slam", "Concussion Blow", "Mocking Blow", "Execute",
        "Intercept", "Whirlwind", "Piercing Howl"}


def test_pummel_has_its_family_flag_on_both_ranks(client):
    # Client 70685: Pummel 6552 and 6554 have family flag 0x8; the stock data has none.
    assert [client[s][FAMILY_FLAGS] for s in PUMMEL_RANKS] == [0x8, 0x8]
