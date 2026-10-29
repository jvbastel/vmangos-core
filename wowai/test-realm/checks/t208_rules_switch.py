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

setup   (rules off) A player Warrior at level 20 learns two vanilla talents: Improved Rend (talent 127,
        rank 1 12286) and Improved Heroic Strike (talent 124, rank 1 12282). The realm stores that it
        logged in under the vanilla rules. After the logout the action bars get the two talent spells
        and Heroic Strike (78, a trainer spell that both rule sets have).
forever (rules on) The first login switches: no vanilla talent in the spellbook (login packet and
        character_spell), their buttons are gone, Heroic Strike and its button stay, the stored rules are
        Forever. Both talents are also in the Forever trees with their stock IDs (#210), so this shows
        that the reset covers them. The free talent points teach Cruelty 5/5 (talent 157, 12320-12856,
        in both rule sets) and the Forever-only Lingering Rage (talent 60002, rank 1 60106). A second
        login under the same rules changes nothing. After the logout the bars get Lingering Rage.
vanilla (rules off) The first login switches back: no Forever spell and no button of it, a talent
        reset (no Cruelty; talent 127 teaches 12286 again), the stored rules are vanilla. A second login
        changes nothing.
"""
import struct
import sys

from kit import Realm, Results
from wowclient import WorldClient

SMSG_INITIAL_SPELLS = 298
SMSG_LEARNED_SPELL = 299
CMSG_LEARN_TALENT = 593
CMSG_LOGOUT_REQUEST = 75
SMSG_LOGOUT_COMPLETE = 77
CMSG_PLAYER_LOGIN = 61
SMSG_LOGIN_VERIFY_WORLD = 566

ACCOUNT = "PLAYER208"
NAME = "Switchwar"
LEVEL = 20
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
BUTTON_REND, BUTTON_IMPROVED_HS, BUTTON_HEROIC_STRIKE, BUTTON_FOREVER = 110, 111, 112, 113
RULES = {"vanilla": 0, "forever": 1}
SWITCH = r"\[Forever\] %s switches from the %s rules to the %s rules"


class SpellClient(WorldClient):
    """Keeps the spells of each SMSG_LEARNED_SPELL and of the last SMSG_INITIAL_SPELLS."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.learned = []
        self.initial_spells = None

    def _observe(self, opcode, body):
        super()._observe(opcode, body)
        if opcode == SMSG_LEARNED_SPELL:
            self.learned.append(struct.unpack_from("<I", body, 0)[0])
        elif opcode == SMSG_INITIAL_SPELLS:
            # uint8 0, uint16 count, then count x (uint16 spell, int16 0), then the cooldowns.
            count = struct.unpack_from("<H", body, 1)[0]
            self.initial_spells = {struct.unpack_from("<H", body, 3 + 4 * i)[0] for i in range(count)}

    def learn_talent(self, talent, rank=0):
        """The spells that the server teaches for one CMSG_LEARN_TALENT."""
        start = len(self.learned)
        self.send(CMSG_LEARN_TALENT, struct.pack("<II", talent, rank))
        self.pump(2)
        return self.learned[start:]

    def logout(self):
        """Logout to the character screen (the server saves the character)."""
        self.send(CMSG_LOGOUT_REQUEST)
        self.wait_for(SMSG_LOGOUT_COMPLETE, 30)
        self.pump(2)

    def login_again(self):
        self.initial_spells = None
        self.send(CMSG_PLAYER_LOGIN, struct.pack("<Q", self.guid))
        self.wait_for(SMSG_LOGIN_VERIFY_WORLD, 30)
        self.pump(3)


mode = sys.argv[1] if len(sys.argv) > 1 else ""
if mode not in ("setup", "forever", "vanilla"):
    sys.exit("usage: t208_rules_switch.py setup|forever|vanilla")

realm = Realm()
results = Results()
realm.mark("t208 " + mode)
c = realm.login(ACCOUNT, NAME, race=2, cls=1, client_class=SpellClient)
me = c.guid
c.pump(2)


def saved_spells():
    c.gm(".save", select=me)
    c.pump(1)
    rows = realm.sql("characters", "SELECT spell FROM character_spell WHERE guid = %d" % me)
    return {int(row) for row in rows.splitlines() if row}


def buttons():
    """button -> spell of the saved spell buttons."""
    c.gm(".save", select=me)
    c.pump(1)
    rows = realm.sql("characters", "SELECT button, action FROM character_action WHERE guid = %d AND type = 0" % me)
    return {int(b): int(a) for b, a in (row.split("\t") for row in rows.splitlines() if row)}


def stored_rules():
    row = realm.sql("characters", "SELECT rules FROM forever_character WHERE guid = %d" % me)
    return int(row) if row else None


def put_buttons(pairs):
    """Puts spells on the bars of the logged out character, as the client would have."""
    values = ", ".join("(%d, %d, %d, 0)" % (me, button, spell) for button, spell in pairs)
    realm.sql("characters", "REPLACE INTO character_action (guid, button, action, type) VALUES " + values)


def check_same_rules_relog(expected_spell):
    """A second login under the same rules: no switch, the spells stay."""
    c.logout()
    realm.mark("t208 relog")
    c.login_again()
    results.check("a login under the same rules does not switch", not realm.wait_log(SWITCH % (NAME, ".*", ".*"), 3, c))
    results.check("a login under the same rules keeps %d" % expected_spell,
                  c.initial_spells is not None and expected_spell in c.initial_spells,
                  "no SMSG_INITIAL_SPELLS" if c.initial_spells is None else "")


if mode == "setup":
    level = int(realm.sql("characters", "SELECT level FROM characters WHERE guid = %d" % me) or 1)
    if level < LEVEL:
        c.gm(".levelup %d" % (LEVEL - level), select=me)
    c.gm(".reset talents", select=me)
    c.pump(1)
    rend = c.learn_talent(IMPROVED_REND)
    heroic = c.learn_talent(IMPROVED_HEROIC_STRIKE)
    results.check("the vanilla talents are learned", rend == [STOCK_REND_1] and heroic == [STOCK_HEROIC_STRIKE_1],
                  "learned %r %r" % (rend, heroic))
    results.check("Heroic Strike is known", HEROIC_STRIKE in saved_spells())
    results.check("the realm stores that the character logged in under the vanilla rules",
                  stored_rules() == RULES["vanilla"], "rules %r" % stored_rules())
    check_same_rules_relog(STOCK_REND_1)
    c.logout()
    put_buttons([(BUTTON_REND, STOCK_REND_1), (BUTTON_IMPROVED_HS, STOCK_HEROIC_STRIKE_1),
                 (BUTTON_HEROIC_STRIKE, HEROIC_STRIKE)])
elif mode == "forever":
    results.check("the first login under the Forever rules switches",
                  realm.wait_log(SWITCH % (NAME, "vanilla", "Forever"), 5, c))
    spells = c.initial_spells or set()
    results.check("the login sends no vanilla talent", c.initial_spells is not None and
                  not spells & {STOCK_REND_1, STOCK_HEROIC_STRIKE_1}, "%r" % (spells & {STOCK_REND_1, STOCK_HEROIC_STRIKE_1}))
    results.check("the login keeps Heroic Strike", HEROIC_STRIKE in spells)
    saved = saved_spells()
    results.check("character_spell has no vanilla talent", not saved & {STOCK_REND_1, STOCK_HEROIC_STRIKE_1})
    bars = buttons()
    results.check("the buttons of the vanilla talents are gone",
                  BUTTON_REND not in bars and BUTTON_IMPROVED_HS not in bars, "%r" % bars)
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
else:
    results.check("the first login under the vanilla rules switches",
                  realm.wait_log(SWITCH % (NAME, "Forever", "vanilla"), 5, c))
    spells = c.initial_spells or set()
    results.check("the login sends no Forever spell", c.initial_spells is not None and FOREVER_LINGERING_RAGE_1 not in spells)
    results.check("the login sends no Cruelty (talent reset)", not spells & set(CRUELTY_SPELLS),
                  "%r" % (spells & set(CRUELTY_SPELLS)))
    results.check("the login keeps Heroic Strike", HEROIC_STRIKE in spells)
    saved = saved_spells()
    results.check("character_spell has no Forever spell", FOREVER_LINGERING_RAGE_1 not in saved)
    results.check("character_spell has no Cruelty", not saved & set(CRUELTY_SPELLS))
    bars = buttons()
    results.check("the button of the Forever spell is gone", BUTTON_FOREVER not in bars, "%r" % bars)
    results.check("the Heroic Strike button stays", bars.get(BUTTON_HEROIC_STRIKE) == HEROIC_STRIKE, "%r" % bars)
    results.check("the realm stores the vanilla rules", stored_rules() == RULES["vanilla"], "rules %r" % stored_rules())
    rend = c.learn_talent(IMPROVED_REND)
    results.check("the talents are reset: Improved Rend teaches the stock rank 1", rend == [STOCK_REND_1],
                  "learned %r" % rend)
    check_same_rules_relog(STOCK_REND_1)

results.finish()
