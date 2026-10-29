#!/usr/bin/env python3
"""In-game check of jvbastel/WOWAI#216: the Vanguard talent swaps Charge for Vanguard Charge, which can be
used in Defensive Stance (spec #204), on the check kit. Each step is one run on the same slot; an acquire
between them restarts the slot with the other rules and keeps the database (same lease):

    wowai/test-realm/realm.sh --ticket N acquire --forever
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t216_vanguard.py forever
    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t216_vanguard.py vanilla

forever (rules on) A level 50 player Warrior with Charge ranks 1 and 2 (100, 6178) and Defensive Stance
        puts Charge rank 2 on a button. In Defensive Stance, stock Charge fails with "Must be in Battle
        Stance". It takes 15 Protection points and Vanguard (talent 60007, spell 60119): the client gets
        Charge rank 2 superseded by Vanguard Charge rank 2 (60128), the button casts 60128, the stock ranks
        stay known but inactive. Vanguard Charge then charges a boar in Defensive Stance. Charge rank 3
        (11578, as from the trainer) gives Vanguard Charge rank 3 (60129) on the same button. A login
        keeps it. A talent reset gives back Charge rank 3 on the same button. Vanguard again, then
        logout.
vanilla (rules off) The switch to vanilla gives back stock Charge rank 3: in the spellbook (login packet,
        active in character_spell) and on the same button. No Vanguard Charge is left.
"""
import struct
import sys

from kit import Realm, Results, kill, pos
from wowclient import WorldClient, unpack_guid

SMSG_SUPERCEDED_SPELL = 300
CMSG_SET_ACTION_BUTTON = 296
SMSG_INITIAL_SPELLS = 298
SMSG_LEARNED_SPELL = 299
CMSG_CAST_SPELL = 302
SMSG_CAST_RESULT = 304
SMSG_SPELL_GO = 306
CMSG_LEARN_TALENT = 593
CMSG_LOGOUT_REQUEST = 75
SMSG_LOGOUT_COMPLETE = 77
CMSG_PLAYER_LOGIN = 61
SMSG_LOGIN_VERIFY_WORLD = 566
TARGET_FLAG_UNIT = 0x2
SPELL_FAILED_ONLY_SHAPESHIFT = 86  # "Must be in %s" (1.12 SpellCastResult)

ACCOUNT = "PLAYER216"
NAME = "Vanguardwar"
LEVEL = 50
DEFENSIVE_STANCE = 71
CHARGE = (100, 6178, 11578)
VANGUARD_CHARGE = (60127, 60128, 60129)
VANGUARD = 60007
VANGUARD_SPELL = 60119
# 15 Protection points (forever/warrior.tsv): Shield Specialization 5, Anticipation 5, Improved Revenge 3,
# Improved Bloodrage 2. Vanguard is on tier 4.
PROTECTION_15 = ((1601, 5), (138, 5), (147, 3), (142, 2))
BUTTON_CHARGE = 115  # a high button, free on a new character
BOAR = 3098
SWITCH = r"\[Forever\] %s switches from the Forever rules to the vanilla rules"


class VanguardClient(WorldClient):
    """Keeps the spell packets that the check reads."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.learned = []
        self.superseded = []
        self.cast_results = []
        self.spell_go = []
        self.initial_spells = None

    def _observe(self, opcode, body):
        super()._observe(opcode, body)
        if opcode == SMSG_LEARNED_SPELL:
            self.learned.append(struct.unpack_from("<H", body, 0)[0])
        elif opcode == SMSG_SUPERCEDED_SPELL:
            self.superseded.append(struct.unpack_from("<HH", body, 0))
        elif opcode == SMSG_CAST_RESULT:
            spell, status = struct.unpack_from("<IB", body, 0)
            reason = body[5] if status == 2 and len(body) > 5 else None
            self.cast_results.append((spell, reason))
        elif opcode == SMSG_SPELL_GO:
            caster, at = unpack_guid(body, 0)
            _, at = unpack_guid(body, at)
            if caster == self.guid:
                self.spell_go.append(struct.unpack_from("<I", body, at)[0])
        elif opcode == SMSG_INITIAL_SPELLS:
            count = struct.unpack_from("<H", body, 1)[0]
            self.initial_spells = {struct.unpack_from("<H", body, 3 + 4 * i)[0] for i in range(count)}

    def learn_talent(self, talent, rank=0):
        start = len(self.learned)
        self.send(CMSG_LEARN_TALENT, struct.pack("<II", talent, rank))
        self.pump(1.5)
        return self.learned[start:]

    def set_button(self, button, spell):
        self.send(CMSG_SET_ACTION_BUTTON, struct.pack("<BI", button, spell))
        self.pump(1)

    def cast(self, spell, target=None):
        """Casts a spell; (its cast results, whether a SPELL_GO of it came)."""
        start_results, start_go = len(self.cast_results), len(self.spell_go)
        if target is None:
            targets = struct.pack("<H", 0)
        else:
            mask, packed = 0, b""
            for i in range(8):
                byte = (target >> (8 * i)) & 0xFF
                if byte:
                    mask |= 1 << i
                    packed += bytes([byte])
            targets = struct.pack("<H", TARGET_FLAG_UNIT) + bytes([mask]) + packed
        self.send(CMSG_CAST_SPELL, struct.pack("<I", spell) + targets)
        self.pump(2)
        results = [r for r in self.cast_results[start_results:] if r[0] == spell]
        return results, spell in self.spell_go[start_go:]

    def logout(self):
        self.send(CMSG_LOGOUT_REQUEST)
        self.wait_for(SMSG_LOGOUT_COMPLETE, 30)
        self.pump(2)

    def login_again(self):
        self.initial_spells = None
        self.send(CMSG_PLAYER_LOGIN, struct.pack("<Q", self.guid))
        self.wait_for(SMSG_LOGIN_VERIFY_WORLD, 30)
        self.pump(3)


mode = sys.argv[1] if len(sys.argv) > 1 else ""
if mode not in ("forever", "vanilla"):
    sys.exit("usage: t216_vanguard.py forever|vanilla")

realm = Realm()
results = Results()
realm.mark("t216 " + mode)
c = realm.login(ACCOUNT, NAME, race=2, cls=1, client_class=VanguardClient)
me = c.guid
c.pump(2)


def charge_rows():
    """spell -> active of the saved Charge and Vanguard Charge rows."""
    c.gm(".save", select=me)
    c.pump(1)
    rows = realm.sql("characters", "SELECT spell, active FROM character_spell WHERE guid = %d AND disabled = 0 "
                     "AND spell IN (%s)" % (me, ", ".join(map(str, CHARGE + VANGUARD_CHARGE))))
    return {int(s): int(a) for s, a in (row.split("\t") for row in rows.splitlines() if row)}


def charge_button():
    rows = realm.sql("characters", "SELECT action FROM character_action WHERE guid = %d AND type = 0 AND button = %d"
                     % (me, BUTTON_CHARGE))
    return int(rows) if rows else None


def learn_vanguard():
    for talent, ranks in PROTECTION_15:
        for rank in range(ranks):
            c.learn_talent(talent, rank)
    start = len(c.superseded)
    learned = c.learn_talent(VANGUARD)
    return learned, c.superseded[start:]


def spawn_boar_ahead():
    """A boar 15 yards from the player (Charge range 8-25), out of combat. Its guid."""
    c.gm(".npc spawn add %d" % BOAR, wait=2.0)
    low = int(realm.sql("mangos", "SELECT MAX(guid) FROM creature WHERE id = %d" % BOAR).split()[-1])
    x, y, z = pos(c, me)
    c.gm(".go xyz %f %f %f" % (x + 15.0, y, z + 1.0), wait=2.0)
    return (0xF130 << 48) | (BOAR << 24) | low


if mode == "forever":
    level = int(realm.sql("characters", "SELECT level FROM characters WHERE guid = %d" % me) or 1)
    if level < LEVEL:
        c.gm(".levelup %d" % (LEVEL - level), select=me)
    c.gm(".reset talents", select=me)
    for spell in CHARGE + VANGUARD_CHARGE:  # a second run starts clean
        c.gm(".unlearn %d" % spell, select=me, wait=0.5)
    for spell in (DEFENSIVE_STANCE, CHARGE[0], CHARGE[1]):
        c.gm(".learn %d" % spell, select=me)
    c.set_button(BUTTON_CHARGE, CHARGE[1])
    rows = charge_rows()
    results.check("Charge rank 2 is on the button", charge_button() == CHARGE[1], "button %r" % charge_button())
    results.check("stock Charge rank 2 is active", rows.get(CHARGE[1]) == 1, "%r" % rows)

    c.cast(DEFENSIVE_STANCE)
    boar = spawn_boar_ahead()
    stock, _ = c.cast(CHARGE[1], boar)
    results.check("stock Charge fails in Defensive Stance (control)",
                  (CHARGE[1], SPELL_FAILED_ONLY_SHAPESHIFT) in stock, "results %r" % stock)

    learned, superseded = learn_vanguard()
    results.check("Vanguard is learned", VANGUARD_SPELL in learned, "learned %r" % learned)
    results.check("the client gets Charge rank 2 superseded by Vanguard Charge rank 2",
                  (CHARGE[1], VANGUARD_CHARGE[1]) in superseded, "superseded %r" % superseded)
    rows = charge_rows()
    results.check("Vanguard Charge rank 2 is active, the stock ranks inactive",
                  rows == {CHARGE[0]: 0, CHARGE[1]: 0, VANGUARD_CHARGE[1]: 1}, "%r" % rows)
    results.check("the button casts Vanguard Charge rank 2", charge_button() == VANGUARD_CHARGE[1],
                  "button %r" % charge_button())

    vanguard, go = c.cast(VANGUARD_CHARGE[1], boar)
    results.check("Vanguard Charge is not refused for the stance",
                  not any(reason == SPELL_FAILED_ONLY_SHAPESHIFT for _, reason in vanguard), "results %r" % vanguard)
    results.check("Vanguard Charge charges in Defensive Stance", go, "results %r" % vanguard)
    kill(c, boar)
    c.pump(2)

    start = len(c.superseded)
    c.gm(".learn %d" % CHARGE[2], select=me)
    results.check("Charge rank 3 gives Vanguard Charge rank 3",
                  (VANGUARD_CHARGE[1], VANGUARD_CHARGE[2]) in c.superseded[start:], "superseded %r" % c.superseded[start:])
    rows = charge_rows()
    results.check("Vanguard Charge rank 3 is the only active Charge",
                  rows == {CHARGE[0]: 0, CHARGE[1]: 0, CHARGE[2]: 0, VANGUARD_CHARGE[2]: 1}, "%r" % rows)
    results.check("the button casts Vanguard Charge rank 3", charge_button() == VANGUARD_CHARGE[2],
                  "button %r" % charge_button())

    c.logout()
    c.login_again()
    spells = c.initial_spells or set()
    results.check("a login keeps Vanguard Charge rank 3 and no stock Charge in the spellbook",
                  VANGUARD_CHARGE[2] in spells and not spells & set(CHARGE), "%r" % (spells & set(CHARGE + VANGUARD_CHARGE)))

    start = len(c.superseded)
    c.gm(".reset talents", select=me)
    c.pump(1)
    results.check("a talent reset gives back Charge rank 3 in the client",
                  (VANGUARD_CHARGE[2], CHARGE[2]) in c.superseded[start:], "superseded %r" % c.superseded[start:])
    rows = charge_rows()
    results.check("after the reset stock Charge rank 3 is active and no Vanguard Charge is known",
                  rows == {CHARGE[0]: 0, CHARGE[1]: 0, CHARGE[2]: 1}, "%r" % rows)
    results.check("after the reset the button casts Charge rank 3", charge_button() == CHARGE[2],
                  "button %r" % charge_button())

    learned, superseded = learn_vanguard()
    results.check("Vanguard again gives Vanguard Charge rank 3", (CHARGE[2], VANGUARD_CHARGE[2]) in superseded,
                  "superseded %r" % superseded)
    c.logout()
    results.check("before the switch the button casts Vanguard Charge rank 3", charge_button() == VANGUARD_CHARGE[2],
                  "button %r" % charge_button())
else:
    results.check("the first login under the vanilla rules switches", realm.wait_log(SWITCH % NAME, 5, c))
    spells = c.initial_spells or set()
    results.check("the login sends stock Charge rank 3 and no Vanguard Charge",
                  c.initial_spells is not None and CHARGE[2] in spells and not spells & set(VANGUARD_CHARGE),
                  "%r" % (spells & set(CHARGE + VANGUARD_CHARGE)))
    rows = charge_rows()
    results.check("stock Charge rank 3 is active, no Vanguard Charge is known",
                  rows.get(CHARGE[2]) == 1 and not set(rows) & set(VANGUARD_CHARGE), "%r" % rows)
    results.check("the button casts stock Charge rank 3 again", charge_button() == CHARGE[2],
                  "button %r" % charge_button())
    c.logout()
    c.login_again()
    results.check("a second login keeps stock Charge rank 3", CHARGE[2] in (c.initial_spells or set()))

results.finish()
