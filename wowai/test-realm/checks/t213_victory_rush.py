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
         dummy for 1 + 15% of attack power (no damage per level, client 70685) and heals the Warrior for 10%
         of its maximum health. It starts the global cooldown: Rend cast right after it is not ready. 16 sec
         after the kill the bit is still set; 21 sec after it the bit is gone and the cast fails again (the
         cooldown is cleared first). A player Rogue that kills a level 30 boar gets no bit.
vanilla: no Victory Rush in the spellbook, no "[Forever] Victorious" line, and no bit 0x20 after a kill of a
         level 30 boar, for the Warrior and for the Rogue.
"""
import re
import sys
import time

from kit import FREEZE, Realm, Results, pos, spawn, stat
from wowclient import MSG_MOVE_HEARTBEAT, SpellClient
SPELL_FAILED_CASTER_AURASTATE = 18   # SpellDefines.h, client build 5875
SPELL_FAILED_NOT_READY = 60          # the same; also the global cooldown

LEVEL = 30
VICTORY_RUSH = 60123
VICTORIOUS_BIT = 1 << (6 - 1)          # aura state 6
# kit.BOAR, the Mottled Boar (Durotar), is level 1: grey for a level 30 player.
REND = 772             # rank 1, on the global cooldown (category 133)
WINDOW = 20
VICTORIOUS_LINE = r"\[Forever\] Victorious: %s killed"


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
    guid = spawn(realm, client)
    if level is not None:
        client.gm(".npc level %d" % level, select=guid)
    return guid


def kill_boar(client, level):
    """The client's character kills a new boar of `level` with `.die`; the aura state right after."""
    boar = spawn_boar(client, level)
    client.gm(".die", select=boar, wait=1.0)
    return aura_state(client)


def login(account, name, cls):
    client = realm.login(account, name, race=2, cls=cls, client_class=SpellClient)
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
reasons, went = w.cast(VICTORY_RUSH, dummy)
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
w.gm(".learn %d" % REND, select=me)
w.gm(".modify rage 1000", select=me)
reasons, went = w.cast(VICTORY_RUSH, dummy, wait=0.3)
# Client 70685: Victory Rush starts the global cooldown (1.5 sec), so Rend right after it is not ready.
rend_reasons, rend_went = w.cast(REND, dummy, wait=0.3)
w.pump(1.7)
results.check("after the kill Victory Rush goes", went and not [r for r in reasons if r is not None], "reasons %r" % reasons)
results.check("Victory Rush starts the global cooldown: Rend right after it is not ready",
              not rend_went and SPELL_FAILED_NOT_READY in rend_reasons, "reasons %r, went %s" % (rend_reasons, rend_went))
damage = before_dummy - stat(w, dummy)[0]
expected = 1 + int(0.15 * ap)
# Client 70685: no damage per level (the old source spell gave 0.9 per level above 20: +9 at level 30).
results.check("Victory Rush has no damage per level",
              not any(abs(damage - (e + 9 * m)) <= 1 for m, e in ((1, expected), (2, 2 * expected))),
              "damage %d; with 0.9 per level it would be %d (or %d on a crit)" % (damage, expected + 9, 2 * expected + 18))
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
reasons, went = w.cast(VICTORY_RUSH, dummy)
results.check("after the window Victory Rush fails with the caster aura state",
              not went and SPELL_FAILED_CASTER_AURASTATE in reasons, "reasons %r, went %s" % (reasons, went))

# Another class gets nothing.
r = login("PLAYER213R", "Victoryrogue", 4)
state = kill_boar(r, LEVEL)
results.check("a Rogue's kill of a level %d boar sets no bit 0x20" % LEVEL, state >= 0 and not state & VICTORIOUS_BIT,
              "aura state %d" % state)
results.check("no Victorious line for the Rogue", not re.findall(VICTORIOUS_LINE % "Victoryrogue", realm.log()))
results.finish()
