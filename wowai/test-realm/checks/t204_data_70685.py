#!/usr/bin/env python3
"""In-game check of the client 1.60.1.70685 numbers of spec jvbastel/WOWAI#204 (final fix pass).

    # A slot with the Forever rules:
    wowai/test-realm/realm.sh --ticket N acquire --forever
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t204_data_70685.py forever
    # The same slot without them:
    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t204_data_70685.py vanilla

A level 60 player Warrior in Battle Stance stands next to frozen boars (GM stun 9454, a lot of health). It casts
with the GM `.cast <spell> triggered`, and reads the boar's threat list with `.list threat` and its own attack
power mods with `.debug getvaluebyname`.

forever: Battle Shout rank 1 (6673, level 1 to 11) gives 9 + 0.3 x 10 = 12 attack power (with the stock 0.5: 14).
         Sunder Armor rank 1 (7386) on a fresh boar gives 34 threat (stock spell_threat 45).
         Demoralizing Shout rank 1 (1160, level 14 to 24) on a fresh boar gives 11 + 0.8 x 10 = 19 threat.
         Revenge rank 1 (6572) logs "[Forever] Revenge" with a bonus of 25% of attack power, and 22.5% in
         Defensive Stance (client hotfix).
vanilla: 15 + 0.5 x 10 = 20 attack power, 45 and 11 threat, and no "[Forever] Revenge" line.
The threat values are before the Battle Stance threat modifier (21156: -20%), which the check applies; the
Sunder/Demoralizing ratio (34/19 against 45/11) holds whatever the stance modifier is.
"""
import re
import sys

from kit import Realm, Results, pos
from wowclient import MSG_MOVE_HEARTBEAT, WorldClient

LEVEL = 60
BOAR = 3098            # Mottled Boar (Durotar), a neutral beast that can be attacked
FREEZE = 9454          # GM stun without an end
BATTLE_SHOUT_1, SUNDER_ARMOR_1, DEMORALIZING_SHOUT_1, REVENGE_1 = 6673, 7386, 1160, 6572
DEFENSIVE_STANCE = 71
BATTLE_STANCE = 2457

mode = sys.argv[1] if len(sys.argv) > 1 else ""
if mode not in ("forever", "vanilla"):
    sys.exit("usage: t204_data_70685.py forever|vanilla")
forever = mode == "forever"

realm = Realm()
results = Results()
realm.mark("t204 70685 " + mode)
name = "Forevernums" if forever else "Vanillanums"   # t211 owns Foreverdata and Vanilladata
w = realm.login("PLAYER204D", name, race=2, cls=1, client_class=WorldClient)
me = w.guid
level = int(realm.sql("characters", "SELECT level FROM characters WHERE guid = %d" % me) or 1)
if level < LEVEL:
    w.gm(".levelup %d" % (LEVEL - level), select=me)
w.gm(".cheat god on", select=me)
w.gm(".cheat criticals off", select=me)
w.set_active_mover()
here = pos(w, me)


def field(name_):
    for line in w.gm(".debug getvaluebyname %s" % name_, select=me, wait=1.0):
        m = re.match(r"%s: (\S+)" % name_, line)
        if m:
            return m.group(1)
    return None


def attack_power_plus():
    return int((field("UNIT_FIELD_ATTACK_POWER_MODS") or "0/0").split("/")[0])


def attack_power():
    base = int(field("UNIT_FIELD_ATTACK_POWER") or 0)
    plus, minus = (int(v) for v in (field("UNIT_FIELD_ATTACK_POWER_MODS") or "0/0").split("/"))
    multiplier = float(field("UNIT_FIELD_ATTACK_POWER_MULTIPLIER") or 0)
    return (base + plus - minus) * (1.0 + multiplier)


def spawn_boar():
    w.gm(".npc spawn add %d" % BOAR, wait=1.5)
    low = int(realm.sql("mangos", "SELECT MAX(guid) FROM creature WHERE id = %d" % BOAR).split()[-1])
    guid = (0xF130 << 48) | (BOAR << 24) | low
    w.gm(".modify hp 1000000 1000000", select=guid)
    w.gm(".aura %d" % FREEZE, select=guid)
    return guid


def threat_of_me(boar):
    """The Warrior's threat on the boar's threat list, or None."""
    for line in w.gm(".list threat", select=boar, wait=1.0):
        m = re.match(r"([-\d.e+]+) - .*%s" % name, line)
        if m:
            return float(m.group(1))
    return None


def delete(boar):
    w.gm(".npc spawn delete", select=boar, wait=1.0)


# Battle Shout rank 1: attack power per level (client 70685: 0.3, stock 0.5).
before = attack_power_plus()
w.gm(".cast %d triggered" % BATTLE_SHOUT_1, select=me, wait=1.5)
gained = attack_power_plus() - before
expected = 12 if forever else 20
results.check("Battle Shout rank 1 at level 60 gives %d attack power" % expected, gained == expected, "gained %d" % gained)
w.gm(".unaura %d" % BATTLE_SHOUT_1, select=me)

# Threat: Sunder Armor rank 1 and Demoralizing Shout rank 1, each on a fresh boar.
w.gm(".cast %d triggered" % BATTLE_STANCE, select=me)   # a run before ends in Defensive Stance
w.move(MSG_MOVE_HEARTBEAT, here[0] - 2.0, here[1], here[2], 0.0)
w.pump(1)
threat = {}
for spell in (SUNDER_ARMOR_1, DEMORALIZING_SHOUT_1):
    boar = spawn_boar()
    w.gm(".cast %d triggered" % spell, select=boar, wait=1.5)
    threat[spell] = threat_of_me(boar)
    delete(boar)
sunder, demo = (34, 19) if forever else (45, 11)
# The Battle Stance passive (21156, SPELL_AURA_MOD_THREAT -20%) scales all threat by 0.8.
STANCE = 0.8
results.check("Sunder Armor rank 1 gives %d threat (x %.1f Battle Stance)" % (sunder, STANCE),
              threat[SUNDER_ARMOR_1] is not None and abs(threat[SUNDER_ARMOR_1] - sunder * STANCE) <= 1,
              "threat %r" % threat)
results.check("Demoralizing Shout rank 1 at level 60 gives %d threat (x %.1f Battle Stance)" % (demo, STANCE),
              threat[DEMORALIZING_SHOUT_1] is not None and abs(threat[DEMORALIZING_SHOUT_1] - demo * STANCE) <= 1,
              "threat %r" % threat)
results.check("the Sunder/Demoralizing threat ratio is %d/%d" % (sunder, demo),
              all(threat.values()) and abs(threat[SUNDER_ARMOR_1] / threat[DEMORALIZING_SHOUT_1] - sunder / demo) < 0.15,
              "threat %r" % threat)

# Revenge rank 1: + 25% of attack power, 22.5% in Defensive Stance (client hotfix).
w.gm(".learn %d" % DEFENSIVE_STANCE, select=me)
for stance, pct in ((None, 25.0), (DEFENSIVE_STANCE, 22.5)):
    label = "Defensive Stance" if stance else "Battle Stance"
    if stance:
        w.gm(".cast %d triggered" % stance, select=me, wait=1.5)
    boar = spawn_boar()
    realm.mark("revenge " + label)
    ap = attack_power()
    w.gm(".cast %d triggered" % REVENGE_1, select=boar, wait=1.5)
    lines = re.findall(r"\[Forever\] Revenge: %s spell %d (\d+) \+ (\d+) \(attack power (\d+)(, Defensive Stance)?\)"
                       % (name, REVENGE_1), realm.log())
    if forever:
        ok = len(lines) == 1 and abs(int(lines[0][1]) - int(lines[0][2]) * pct / 100) <= 1 and \
            bool(lines[0][3]) == bool(stance) and abs(int(lines[0][2]) - ap) <= max(2, ap * 0.02)
        results.check("Revenge in %s adds %g%% of attack power (%.0f AP)" % (label, pct, ap), ok, "lines %r" % lines)
    else:
        results.check("no Forever Revenge line in %s" % label, not lines, "lines %r" % lines)
    delete(boar)

w.gm(".cheat god off", select=me)
results.finish()
