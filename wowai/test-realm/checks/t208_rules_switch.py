#!/usr/bin/env python3
"""In-game check of jvbastel/WOWAI#208: a Warrior switches cleanly between the vanilla and the Forever rules
(spec #204), on the check kit. Each step is one run on the same slot; an acquire between them restarts
the slot with the other rules and keeps the database (same lease):

    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t208_rules_switch.py setup
    wowai/test-realm/realm.sh --ticket N acquire --forever
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t208_rules_switch.py forever
    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t208_rules_switch.py vanilla
    wowai/test-realm/realm.sh --ticket N acquire --forever
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t208_rules_switch.py forever-again

setup   (rules off) A player Warrior at level 30 learns vanilla talents: Improved Rend (talent 127,
        rank 1 12286), Improved Heroic Strike (talent 124, rank 1 12282), Cruelty 5/5 (talent 157) and
        Improved Demoralizing Shout (talent 161, rank 1 12324). The last one is not in the Forever trees:
        only the stock-talent step of the switch (LoadStockTalents) takes it away. The realm stores that
        it logged in under the vanilla rules. After the logout the action bars get Improved Rend,
        Improved Heroic Strike, Improved Demoralizing Shout and Heroic Strike (78, a trainer spell that
        both rule sets have).
forever (rules on) The first login switches: no vanilla talent in the spellbook (login packet and
        character_spell), their buttons are gone, Heroic Strike and its button stay, the stored rules are
        Forever. Both talents are also in the Forever trees with their stock IDs (#210), so this shows
        that the reset covers them. The free talent points teach Cruelty 5/5 (talent 157, 12320-12856,
        in both rule sets) and the Forever-only Lingering Rage (talent 60002, rank 1 60106). A second
        login under the same rules changes nothing. After the logout the bars get Lingering Rage.
        The switch also teaches Berserker Rage (18499), a Forever baseline spell at level 30 (stock 32).
        Then a second Warrior, the sleeper, logs in under the Forever rules, reaches level 30, learns
        Cruelty 5/5 and Lingering Rage, and logs out. It does not log in again until forever-again.
vanilla (rules off) The first login switches back: no Forever spell and no button of it, a talent
        reset (no Cruelty; talent 127 teaches 12286 again), no Berserker Rage below its stock level 32
        (#208, user story 21), the stored rules are vanilla. A second login changes nothing. The login
        of the first Warrior does not take the Forever spells of the sleeper away (review S1: the stock
        load deleted the rows of an unknown spell for all characters): its character_spell rows stay.
forever-again (rules on) The sleeper logs in under the rules it last played under: no switch, and it
        still has Lingering Rage and Cruelty 5/5 (review S1: rules 1 -> 0 -> 1). The first Warrior
        switches to the Forever rules again.
"""
import sys

from kit import Realm, Results, saved_spells
from wowclient import SpellClient

ACCOUNT = "PLAYER208"
NAME = "Switchwar"
SLEEPER_ACCOUNT = "PLAYER208B"
SLEEPER = "Sleepwar"
LEVEL = 30
BERSERKER_RAGE = 18499
IMPROVED_REND = 127
IMPROVED_HEROIC_STRIKE = 124
STOCK_REND_1 = 12286
STOCK_HEROIC_STRIKE_1 = 12282
CRUELTY = 157
CRUELTY_SPELLS = (12320, 12852, 12853, 12855, 12856)
LINGERING_RAGE = 60002
FOREVER_LINGERING_RAGE_1 = 60106
HEROIC_STRIKE = 78
# Action bar buttons that the check owns (0-119; the high ones are free on a new character).
BUTTON_REND, BUTTON_IMPROVED_HS, BUTTON_HEROIC_STRIKE, BUTTON_FOREVER, BUTTON_IMPROVED_DS = 110, 111, 112, 113, 114
IMPROVED_DEMORALIZING_SHOUT = 161
STOCK_IMPROVED_DEMORALIZING_SHOUT_1 = 12324
RULES = {"vanilla": 0, "forever": 1}
SWITCH = r"\[Forever\] %s switches from the %s rules to the %s rules"


mode = sys.argv[1] if len(sys.argv) > 1 else ""
if mode not in ("setup", "forever", "vanilla", "forever-again"):
    sys.exit("usage: t208_rules_switch.py setup|forever|vanilla|forever-again")

realm = Realm()
results = Results()
realm.mark("t208 " + mode)
c = realm.login(ACCOUNT, NAME, race=2, cls=1, client_class=SpellClient)
me = c.guid
c.pump(2)


def buttons():
    """button -> spell of the saved spell buttons."""
    c.gm(".save", select=me)
    c.pump(1)
    rows = realm.sql("characters", "SELECT button, action FROM character_action WHERE guid = %d AND type = 0" % me)
    return {int(b): int(a) for b, a in (row.split("\t") for row in rows.splitlines() if row)}


def stored_rules(guid=None):
    row = realm.sql("characters", "SELECT rules FROM forever_character WHERE guid = %d" % (guid or me))
    return int(row) if row else None


def rows_of(guid):
    """The enabled character_spell rows of a character that is not logged in (no .save)."""
    rows = realm.sql("characters", "SELECT spell FROM character_spell WHERE guid = %d AND disabled = 0" % guid)
    return {int(row) for row in rows.splitlines() if row}


def level_to(client, level):
    have = int(realm.sql("characters", "SELECT level FROM characters WHERE guid = %d" % client.guid) or 1)
    if have < level:
        client.gm(".levelup %d" % (level - have), select=client.guid)


def put_buttons(pairs):
    """Puts spells on the bars of the logged out character, as the client would have."""
    values = ", ".join("(%d, %d, %d, 0)" % (me, button, spell) for button, spell in pairs)
    realm.sql("characters", "REPLACE INTO character_action (guid, button, action, type) VALUES " + values)


def saved_rows():
    """The character_spell and character_action rows as they are, without a .save first."""
    return (realm.sql("characters", "SELECT spell, disabled FROM character_spell WHERE guid = %d ORDER BY spell" % me),
            realm.sql("characters", "SELECT button, action, type FROM character_action WHERE guid = %d ORDER BY button" % me))


def check_same_rules_relog(expected_spell):
    """A second login under the same rules: no switch, the spells stay, the login writes no spell or button."""
    c.logout()
    before = saved_rows()
    realm.mark("t208 relog")
    c.login_again()
    results.check("a login under the same rules writes no spell or button row", saved_rows() == before)
    results.check("a login under the same rules does not switch", not realm.wait_log(SWITCH % (NAME, ".*", ".*"), 3, c))
    results.check("a login under the same rules keeps %d" % expected_spell,
                  c.initial_spells is not None and expected_spell in c.initial_spells,
                  "no SMSG_INITIAL_SPELLS" if c.initial_spells is None else "")


if mode == "setup":
    level_to(c, LEVEL)
    c.gm(".reset talents", select=me)
    c.pump(1)
    rend = c.learn_talent(IMPROVED_REND)
    heroic = c.learn_talent(IMPROVED_HEROIC_STRIKE)
    cruelty = [spell for rank in range(5) for spell in c.learn_talent(CRUELTY, rank)]
    demoralizing = c.learn_talent(IMPROVED_DEMORALIZING_SHOUT)
    results.check("the vanilla talents are learned",
                  rend == [STOCK_REND_1] and heroic == [STOCK_HEROIC_STRIKE_1] and cruelty == list(CRUELTY_SPELLS)
                  and demoralizing == [STOCK_IMPROVED_DEMORALIZING_SHOUT_1],
                  "learned %r %r %r %r" % (rend, heroic, cruelty, demoralizing))
    # A default spell is "dependent": the login packet has it, character_spell never does.
    results.check("Heroic Strike is known", c.initial_spells is not None and HEROIC_STRIKE in c.initial_spells)
    results.check("the realm stores that the character logged in under the vanilla rules",
                  stored_rules() == RULES["vanilla"], "rules %r" % stored_rules())
    check_same_rules_relog(STOCK_REND_1)
    c.logout()
    put_buttons([(BUTTON_REND, STOCK_REND_1), (BUTTON_IMPROVED_HS, STOCK_HEROIC_STRIKE_1),
                 (BUTTON_HEROIC_STRIKE, HEROIC_STRIKE), (BUTTON_IMPROVED_DS, STOCK_IMPROVED_DEMORALIZING_SHOUT_1)])
elif mode == "forever":
    VANILLA_TALENTS = {STOCK_REND_1, STOCK_HEROIC_STRIKE_1, STOCK_IMPROVED_DEMORALIZING_SHOUT_1} | set(CRUELTY_SPELLS)
    results.check("the first login under the Forever rules switches",
                  realm.wait_log(SWITCH % (NAME, "vanilla", "Forever"), 5, c))
    spells = c.initial_spells or set()
    results.check("the login sends no vanilla talent", c.initial_spells is not None and
                  not spells & VANILLA_TALENTS, "%r" % (spells & VANILLA_TALENTS))
    results.check("the login keeps Heroic Strike", HEROIC_STRIKE in spells)
    results.check("the switch teaches the baseline Berserker Rage at level %d" % LEVEL, BERSERKER_RAGE in spells)
    saved = saved_spells(realm, c)
    results.check("character_spell has no vanilla talent", not saved & VANILLA_TALENTS)
    bars = buttons()
    results.check("the buttons of the vanilla talents are gone",
                  BUTTON_REND not in bars and BUTTON_IMPROVED_HS not in bars and BUTTON_IMPROVED_DS not in bars,
                  "%r" % bars)
    results.check("the Heroic Strike button stays", bars.get(BUTTON_HEROIC_STRIKE) == HEROIC_STRIKE, "%r" % bars)
    results.check("the realm stores the Forever rules", stored_rules() == RULES["forever"], "rules %r" % stored_rules())
    cruelty = [spell for rank in range(5) for spell in c.learn_talent(CRUELTY, rank)]
    lingering = c.learn_talent(LINGERING_RAGE)
    results.check("the talent points are free: Cruelty 5/5 and the Forever Lingering Rage are learned",
                  cruelty == list(CRUELTY_SPELLS) and lingering == [FOREVER_LINGERING_RAGE_1],
                  "learned %r %r" % (cruelty, lingering))
    check_same_rules_relog(FOREVER_LINGERING_RAGE_1)
    c.logout()
    put_buttons([(BUTTON_FOREVER, FOREVER_LINGERING_RAGE_1)])

    # The sleeper: a Forever Warrior that stays logged out while the rules are off (review S1).
    realm.mark("t208 sleeper")
    s = realm.login(SLEEPER_ACCOUNT, SLEEPER, race=2, cls=1, client_class=SpellClient)
    s.pump(2)
    level_to(s, LEVEL)
    s.gm(".reset talents", select=s.guid)
    s.pump(1)
    cruelty = s.learn_ranks(CRUELTY, 5)
    lingering = s.learn_talent(LINGERING_RAGE)
    results.check("the sleeper learns Cruelty 5/5 and Lingering Rage under the Forever rules",
                  cruelty == list(CRUELTY_SPELLS) and lingering == [FOREVER_LINGERING_RAGE_1],
                  "learned %r %r" % (cruelty, lingering))
    s.logout()
    results.check("the sleeper is saved with its Forever talent", FOREVER_LINGERING_RAGE_1 in rows_of(s.guid))
    results.check("the realm stores the Forever rules for the sleeper", stored_rules(s.guid) == RULES["forever"],
                  "rules %r" % stored_rules(s.guid))
elif mode == "vanilla":
    results.check("the first login under the vanilla rules switches",
                  realm.wait_log(SWITCH % (NAME, "Forever", "vanilla"), 5, c))
    spells = c.initial_spells or set()
    results.check("the login sends no Forever spell", c.initial_spells is not None and FOREVER_LINGERING_RAGE_1 not in spells)
    results.check("the login sends no Cruelty (talent reset)", not spells & set(CRUELTY_SPELLS),
                  "%r" % (spells & set(CRUELTY_SPELLS)))
    results.check("the login keeps Heroic Strike", HEROIC_STRIKE in spells)
    results.check("no Berserker Rage below its stock level 32", BERSERKER_RAGE not in spells)
    saved = saved_spells(realm, c)
    results.check("character_spell has no Forever spell", FOREVER_LINGERING_RAGE_1 not in saved)
    results.check("character_spell has no Berserker Rage", BERSERKER_RAGE not in saved)
    results.check("character_spell has no Cruelty", not saved & set(CRUELTY_SPELLS))
    bars = buttons()
    results.check("the button of the Forever spell is gone", BUTTON_FOREVER not in bars, "%r" % bars)
    results.check("the Heroic Strike button stays", bars.get(BUTTON_HEROIC_STRIKE) == HEROIC_STRIKE, "%r" % bars)
    results.check("the realm stores the vanilla rules", stored_rules() == RULES["vanilla"], "rules %r" % stored_rules())
    rend = c.learn_talent(IMPROVED_REND)
    results.check("the talents are reset: Improved Rend teaches the stock rank 1", rend == [STOCK_REND_1],
                  "learned %r" % rend)
    check_same_rules_relog(STOCK_REND_1)
    sleeper = realm.guid_of(SLEEPER)
    kept = rows_of(sleeper)
    # Only the top Cruelty rank has a row: a talent rank replaces the rank below it (stock Player::_SaveSpells).
    results.check("a vanilla login keeps the Forever spells of a Warrior that is not logged in",
                  FOREVER_LINGERING_RAGE_1 in kept and CRUELTY_SPELLS[-1] in kept, "rows %r" % sorted(kept))
    results.check("the sleeper still has the Forever rules stored", stored_rules(sleeper) == RULES["forever"],
                  "rules %r" % stored_rules(sleeper))
else:
    # forever-again: the first Warrior switches again; then the sleeper logs in.
    results.check("the first Warrior switches to the Forever rules again",
                  realm.wait_log(SWITCH % (NAME, "vanilla", "Forever"), 5, c))
    c.logout()
    realm.mark("t208 sleeper again")
    s = realm.login(SLEEPER_ACCOUNT, SLEEPER, race=2, cls=1, client_class=SpellClient)
    s.pump(2)
    results.check("the sleeper does not switch: it last played under the Forever rules",
                  not realm.wait_log(SWITCH % (SLEEPER, ".*", ".*"), 3, s))
    spells = s.initial_spells or set()
    results.check("the sleeper still has Lingering Rage after rules 1 -> 0 -> 1",
                  FOREVER_LINGERING_RAGE_1 in spells, "no SMSG_INITIAL_SPELLS" if s.initial_spells is None else "")
    results.check("the sleeper still has Cruelty 5/5", set(CRUELTY_SPELLS) <= spells)
    results.check("the sleeper has Berserker Rage at level %d" % LEVEL, BERSERKER_RAGE in spells)

results.finish()
