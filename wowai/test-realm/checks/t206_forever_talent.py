#!/usr/bin/env python3
"""In-game check of jvbastel/WOWAI#206, the tracer bullet of the Forever rules (spec #204), on the check kit.

    # A slot with the Forever rules:
    wowai/test-realm/realm.sh --ticket N acquire --forever
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t206_forever_talent.py forever
    # The same slot without them:
    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t206_forever_talent.py vanilla

A player Warrior at level 20 learns talents with CMSG_LEARN_TALENT, as the talent window does. The talent
IDs and spells come from forever/warrior.tsv (one row: Improved Rend, talent 127, Forever rank spells
60000-60002) and from the stock Talent.dbc (Improved Rend ranks 12286, ...; Improved Heroic Strike,
talent 124, rank 1 12282).

forever: talent 127 teaches the Forever rank 1 (60000), not the stock one. Talent 124 is not in the
         Forever Warrior trees, so the server refuses it. After a logout and a new login, the
         character still has 60000: in character_spell and in the SMSG_INITIAL_SPELLS of the login.
vanilla: the vanilla Warrior trees: talent 127 teaches 12286, talent 124 teaches 12282, and no
         Forever spell is learned.
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

IMPROVED_REND = 127
IMPROVED_HEROIC_STRIKE = 124
FOREVER_REND_1 = 60000
FOREVER_SPELLS = (60000, 60001, 60002)
STOCK_REND_1 = 12286
STOCK_HEROIC_STRIKE_1 = 12282
LEVEL = 20


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

    def relog(self):
        """Logout to the character screen, then log the same character in again."""
        self.send(CMSG_LOGOUT_REQUEST)
        self.wait_for(SMSG_LOGOUT_COMPLETE, 30)
        self.initial_spells = None
        self.send(CMSG_PLAYER_LOGIN, struct.pack("<Q", self.guid))
        self.wait_for(SMSG_LOGIN_VERIFY_WORLD, 30)
        self.pump(3)


mode = sys.argv[1] if len(sys.argv) > 1 else ""
if mode not in ("forever", "vanilla"):
    sys.exit("usage: t206_forever_talent.py forever|vanilla")

realm = Realm()
results = Results()
name = "Foreverwar" if mode == "forever" else "Vanillawar"
c = realm.login("PLAYER206", name, race=2, cls=1, client_class=SpellClient)
me = c.guid
level = int(realm.sql("characters", "SELECT level FROM characters WHERE guid = %d" % me) or 1)
if level < LEVEL:
    c.gm(".levelup %d" % (LEVEL - level), select=me)
c.gm(".reset talents", select=me)
c.pump(1)


def saved_spells():
    c.gm(".save", select=me)
    rows = realm.sql("characters", "SELECT spell FROM character_spell WHERE guid = %d" % me)
    return {int(row) for row in rows.splitlines() if row}


rend = c.learn_talent(IMPROVED_REND)
heroic = c.learn_talent(IMPROVED_HEROIC_STRIKE)

if mode == "forever":
    results.check("Improved Rend teaches the Forever rank 1", rend == [FOREVER_REND_1], "learned %r" % rend)
    results.check("Improved Heroic Strike is not in the Forever Warrior trees", heroic == [], "learned %r" % heroic)
    results.check("the Forever rank is saved", FOREVER_REND_1 in saved_spells())
    c.relog()
    results.check("after a relog the login sends the Forever rank",
                  c.initial_spells is not None and FOREVER_REND_1 in c.initial_spells,
                  "no SMSG_INITIAL_SPELLS" if c.initial_spells is None else "")
    saved = saved_spells()
    results.check("after a relog the character still has the Forever rank", FOREVER_REND_1 in saved)
    results.check("the character has no stock Improved Rend", STOCK_REND_1 not in saved)
else:
    results.check("Improved Rend teaches the stock rank 1", rend == [STOCK_REND_1], "learned %r" % rend)
    results.check("Improved Heroic Strike is in the vanilla Arms tree", heroic == [STOCK_HEROIC_STRIKE_1],
                  "learned %r" % heroic)
    saved = saved_spells()
    results.check("both stock ranks are saved", {STOCK_REND_1, STOCK_HEROIC_STRIKE_1} <= saved)
    results.check("no Forever spell is learned", not saved & set(FOREVER_SPELLS), "%r" % (saved & set(FOREVER_SPELLS)))

results.finish()
