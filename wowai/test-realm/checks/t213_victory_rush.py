#!/usr/bin/env python3
"""In-game check of jvbastel/WOWAI#213 (spec #204): Victory Rush under the Forever rules.

    # A slot with the Forever rules:
    wowai/test-realm/realm.sh --ticket N acquire --forever
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t213_victory_rush.py forever
    # The same slot without them:
    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t213_victory_rush.py vanilla

A level 30 player Warrior stands next to a frozen boar with a lot of health (the dummy for the casts). It
kills other boars with the GM `.die` (the killing blow is the Warrior's). The client reads the Warrior's
UNIT_FIELD_AURASTATE with `.debug getvaluebyname`: the client greys out Victory Rush unless the caster
aura state of its Spell.dbc row (6, bit 0x20) is set there. The cast itself is a CMSG_CAST_SPELL at the dummy.

forever: Victory Rush (60123) is in the spellbook. A kill of a grey boar (level 1) gives no Victorious, no
         aura state bit, and the cast fails with "You can't do that yet" (SPELL_FAILED_CASTER_AURASTATE). A
         kill of a level 30 boar logs "[Forever] Victorious", sets the bit, and the cast goes: it hits the
         dummy for 1 + 15% of attack power and heals the Warrior for 10% of its maximum health. 16 sec
         after the kill the bit is still set; 21 sec after it the bit is gone and the cast fails again (the
         cooldown is cleared first). A player Rogue that kills a level 30 boar gets no bit.
vanilla: no Victory Rush in the spellbook, no "[Forever] Victorious" line, and no bit 0x20 after a kill of a
         level 30 boar, for the Warrior and for the Rogue.
"""
import re
import struct
import sys
import time

from kit import Realm, Results, pos, stat
from wowclient import MSG_MOVE_HEARTBEAT, WorldClient, unpack_guid

CMSG_CAST_SPELL = 302
SMSG_CAST_RESULT = 304
SMSG_SPELL_GO = 306
SMSG_INITIAL_SPELLS = 298
SMSG_LEARNED_SPELL = 299
TARGET_FLAG_UNIT = 0x2
SPELL_FAILED_CASTER_AURASTATE = 18   # SpellDefines.h, client build 5875

LEVEL = 30
VICTORY_RUSH = 60123
VICTORIOUS_BIT = 1 << (6 - 1)          # aura state 6
BOAR = 3098            # Mottled Boar (Durotar), level 1: grey for a level 30 player
FREEZE = 9454          # GM stun without an end
WINDOW = 20
VICTORIOUS_LINE = r"\[Forever\] Victorious: %s killed"


class RushClient(WorldClient):
    """Keeps the spell packets that the check reads."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.cast_results = []
        self.spell_go = []
        self.initial_spells = None
        self.learned = []

    def knows(self, spell):
        """The spell is in the spellbook: at login, or learned since (a level-up teaches baseline spells)."""
        return spell in (self.initial_spells or set()) or spell in self.learned

    def _observe(self, opcode, body):
        super()._observe(opcode, body)
        try:
            if opcode == SMSG_CAST_RESULT:
                spell, status = struct.unpack_from("<IB", body, 0)
                reason = body[5] if status == 2 and len(body) > 5 else None
                self.cast_results.append((spell, reason))
            elif opcode == SMSG_SPELL_GO:
                caster, at = unpack_guid(body, 0)
                _, at = unpack_guid(body, at)
                if caster == self.guid:
                    self.spell_go.append(struct.unpack_from("<I", body, at)[0])
            elif opcode == SMSG_LEARNED_SPELL:
                self.learned.append(struct.unpack_from("<H", body, 0)[0])
            elif opcode == SMSG_INITIAL_SPELLS:
                count = struct.unpack_from("<H", body, 1)[0]
                self.initial_spells = {struct.unpack_from("<H", body, 3 + 4 * i)[0] for i in range(count)}
        except (IndexError, struct.error):
            pass

    def cast_at(self, spell, target):
        """Casts a spell at a unit; (the fail reasons of its cast results, whether a SPELL_GO of it came)."""
        start_results, start_go = len(self.cast_results), len(self.spell_go)
        mask, packed = 0, b""
        for i in range(8):
            byte = (target >> (8 * i)) & 0xFF
            if byte:
                mask |= 1 << i
                packed += bytes([byte])
        self.send(CMSG_CAST_SPELL, struct.pack("<IH", spell, TARGET_FLAG_UNIT) + bytes([mask]) + packed)
        self.pump(2)
        reasons = [r for s, r in self.cast_results[start_results:] if s == spell]
        return reasons, spell in self.spell_go[start_go:]


mode = sys.argv[1] if len(sys.argv) > 1 else ""
if mode not in ("forever", "vanilla"):
    sys.exit("usage: t213_victory_rush.py forever|vanilla")
forever = mode == "forever"

realm = Realm()
results = Results()
realm.mark("t213 " + mode)


def field(client, guid, name):
    """The value of an update field of a unit from `.debug getvaluebyname`, as text, or None."""
    for line in client.gm(".debug getvaluebyname %s" % name, select=guid, wait=1.0):
        m = re.match(r"%s: (\S+)" % name, line)
        if m:
            return m.group(1)
    return None


def aura_state(client):
    value = field(client, client.guid, "UNIT_FIELD_AURASTATE")
    return int(value) if value is not None else -1


def attack_power(client):
    base = int(field(client, client.guid, "UNIT_FIELD_ATTACK_POWER") or 0)
    plus, minus = (int(v) for v in (field(client, client.guid, "UNIT_FIELD_ATTACK_POWER_MODS") or "0/0").split("/"))
    multiplier = float(field(client, client.guid, "UNIT_FIELD_ATTACK_POWER_MULTIPLIER") or 0)
    return (base + plus - minus) * (1.0 + multiplier)


def spawn_boar(client, level=None):
    """A boar at the client's place; its guid."""
    client.gm(".npc spawn add %d" % BOAR, wait=1.5)
    low = int(realm.sql("mangos", "SELECT MAX(guid) FROM creature WHERE id = %d" % BOAR).split()[-1])
    guid = (0xF130 << 48) | (BOAR << 24) | low
    if level is not None:
        client.gm(".npc level %d" % level, select=guid)
    return guid


def kill_boar(client, level):
    """The client's character kills a new boar of `level` with `.die`; the aura state right after."""
    boar = spawn_boar(client, level)
    client.gm(".die", select=boar, wait=1.0)
    return aura_state(client)


def login(account, name, cls):
    client = realm.login(account, name, race=2, cls=cls, client_class=RushClient)
    level = int(realm.sql("characters", "SELECT level FROM characters WHERE guid = %d" % client.guid) or 1)
    if level < LEVEL:
        client.gm(".levelup %d" % (LEVEL - level), select=client.guid)
    client.gm(".cheat criticals off", select=client.guid)
    client.pump(1)
    return client


name = "Victorywar" if forever else "Vanillawar"
# A new character reaches level 30 with `.levelup`, which teaches the baseline spells of each level (#212).
w = login("PLAYER213", name, 1)
me = w.guid
w.set_active_mover()

if not forever:
    results.check("no Victory Rush in the spellbook under the vanilla rules",
                  w.initial_spells is not None and not w.knows(VICTORY_RUSH))
    start = aura_state(w)
    results.check("the Warrior's aura state has no bit 0x20 before a kill", start >= 0 and not start & VICTORIOUS_BIT,
                  "aura state %d" % start)
    state = kill_boar(w, LEVEL)
    results.check("a kill of a level %d boar sets no bit 0x20 for the Warrior" % LEVEL, state >= 0 and not state & VICTORIOUS_BIT,
                  "aura state %d" % state)
    results.check("no Forever Victorious line", not re.findall(VICTORIOUS_LINE % name, realm.log()))
    r = login("PLAYER213R", "Vanillarogue", 4)
    state = kill_boar(r, LEVEL)
    results.check("a kill of a level %d boar sets no bit 0x20 for a Rogue" % LEVEL, state >= 0 and not state & VICTORIOUS_BIT,
                  "aura state %d" % state)
    results.finish()

results.check("Victory Rush is in the spellbook at level %d" % LEVEL,
              w.knows(VICTORY_RUSH))

# The dummy at the Warrior's place; the Warrior steps 2 yards west of it and faces east, towards it.
here = pos(w, me)
dummy = spawn_boar(w)
w.gm(".modify hp 1000000 1000000", select=dummy)
w.gm(".aura %d" % FREEZE, select=dummy)
w.move(MSG_MOVE_HEARTBEAT, here[0] - 2.0, here[1], here[2], 0.0)
w.pump(1)
w.gm(".cooldown clear", select=me)

# A grey kill.
state = kill_boar(w, None)
results.check("a kill of a grey boar sets no bit 0x20", state >= 0 and not state & VICTORIOUS_BIT, "aura state %d" % state)
reasons, went = w.cast_at(VICTORY_RUSH, dummy)
results.check("after a grey kill Victory Rush fails with the caster aura state",
              not went and SPELL_FAILED_CASTER_AURASTATE in reasons, "reasons %r, went %s" % (reasons, went))

# A kill that yields experience.
realm.mark("xp kill")
state = kill_boar(w, LEVEL)
killed_at = time.time()
results.check("a kill of a level %d boar sets bit 0x20 (the client shows Victory Rush usable)" % LEVEL,
              state >= 0 and bool(state & VICTORIOUS_BIT), "aura state %d" % state)
results.check("the server logs the Victorious line", bool(re.findall(VICTORIOUS_LINE % name, realm.log())))
_, max_health, _, _ = stat(w, me)
w.gm(".modify hp %d %d" % (max_health // 2, max_health), select=me)
before_heal = stat(w, me)[0]
before_dummy = stat(w, dummy)[0]
ap = attack_power(w)
reasons, went = w.cast_at(VICTORY_RUSH, dummy)
results.check("after the kill Victory Rush goes", went and not [r for r in reasons if r is not None], "reasons %r" % reasons)
damage = before_dummy - stat(w, dummy)[0]
expected = 1 + int(0.15 * ap)
# `.cheat criticals off` leaves the natural crit chance: a melee crit doubles the hit.
results.check("Victory Rush deals 1 + 15%% of attack power (%.0f AP)" % ap,
              any(abs(damage - e) <= max(2, e * 0.05) for e in (expected, 2 * expected)),
              "damage %d, expected %d (or %d on a crit)" % (damage, expected, 2 * expected))
healed = stat(w, me)[0] - before_heal
results.check("Victory Rush heals 10%% of maximum health (%d)" % max_health,
              max_health // 10 <= healed <= max_health // 10 + max_health // 50, "healed %d" % healed)

# The window: still usable 16 sec after the kill, gone 21 sec after it.
w.pump(max(0.0, 16 - (time.time() - killed_at)))
state = aura_state(w)
results.check("16 sec after the kill bit 0x20 is still set", state >= 0 and bool(state & VICTORIOUS_BIT), "aura state %d" % state)
w.pump(max(0.0, WINDOW + 1 - (time.time() - killed_at)))
state = aura_state(w)
results.check("%d sec after the kill bit 0x20 is gone" % (WINDOW + 1), state >= 0 and not state & VICTORIOUS_BIT,
              "aura state %d" % state)
w.gm(".cooldown clear", select=me)
reasons, went = w.cast_at(VICTORY_RUSH, dummy)
results.check("after the window Victory Rush fails with the caster aura state",
              not went and SPELL_FAILED_CASTER_AURASTATE in reasons, "reasons %r, went %s" % (reasons, went))

# Another class gets nothing.
r = login("PLAYER213R", "Victoryrogue", 4)
state = kill_boar(r, LEVEL)
results.check("a Rogue's kill of a level %d boar sets no bit 0x20" % LEVEL, state >= 0 and not state & VICTORIOUS_BIT,
              "aura state %d" % state)
results.check("no Victorious line for the Rogue", not re.findall(VICTORIOUS_LINE % "Victoryrogue", realm.log()))
results.finish()
