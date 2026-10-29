#!/usr/bin/env python3
"""In-game check of jvbastel/WOWAI#214 (spec #204): rage from weapon speed and Warrior DoT crits.

    # A slot with the Forever rules:
    wowai/test-realm/realm.sh --ticket N acquire --forever
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t214_forever_warrior.py forever
    # The same slot without them:
    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t214_forever_warrior.py vanilla

A level 60 player Warrior swings at a frozen boar (GM stun 9454, a lot of health, so it neither dies nor
hits back): 10 sec with a slow two-hand axe (Large Axe, 2.8 sec), then 10 sec with a fast one-hand axe
(Hand Axe, 2.0 sec). The server logs a "[Forever] Warrior rage" line per hit under the Forever rules; the
check reads the Warrior's rage with `.unit statinfo` and counts the hits from SMSG_ATTACKERSTATEUPDATE.
Then, with `.cheat criticals on` for both, a player Rogue and the Warrior each cast Rend rank 1 (772) on
the boar with `.cast 772 triggered`, and the Rogue casts Shadow Word: Pain rank 1 (589). Each client reads
the tick damage of its own DoTs from SMSG_PERIODICAURALOG. Last, the Warrior learns Deep Wounds 3/3 (12867)
and swings with always-crit, so each hit applies Deep Wound (12721).

forever: each hit gives the rage of the formula in forever/warrior.tsv (speed x 3.46 one-hand, x 4.5
         two-hand, x2 on a crit), whatever the damage, and the rage that the Warrior has is their sum.
         The slow axe gives more rage per hit than the fast one. Each Rend tick of the Warrior crits (a log
         line, and twice the Rogue's tick); the Rogue's Rend and Shadow Word: Pain ticks never crit. Deep
         Wound ticks of the Warrior crit.
vanilla: no "[Forever]" line. A hit gives the stock rage, from the damage: at level 60 a weak axe gives far
         less than the Forever value. The Warrior's Rend ticks are the same as the Rogue's.
"""
import re
import struct
import sys
import time

from kit import Realm, Results, clear_backpack, stand_before
from wowclient import WorldClient, unpack_guid

CMSG_AUTOEQUIP_ITEM = 266
CMSG_ATTACKSWING = 321
CMSG_ATTACKSTOP = 322
SMSG_ATTACKERSTATEUPDATE = 330
SMSG_PERIODICAURALOG = 590
SPELL_AURA_PERIODIC_DAMAGE = 3

LEVEL = 60
BOAR = 3098            # Mottled Boar (Durotar), a neutral beast that can be attacked
FREEZE = 9454          # GM stun without an end
ONE_HAND_AXES, TWO_HAND_AXES = 196, 197
LARGE_AXE = (2491, 2800, True)   # entry, speed ms, two-hand
HAND_AXE = (2134, 2000, False)
REND_1 = 772
SHADOW_WORD_PAIN_1 = 589
DEEP_WOUNDS_3 = 12867
DEEP_WOUND = 12721
BACKPACK = 255
MAIN_HAND_SLOT = 15
SWING_SECONDS = 10


class CombatClient(WorldClient):
    """Keeps the melee hits (damage) and the periodic damage ticks (spell, damage) of its own character."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.hits = []
        self.ticks = []

    def _observe(self, opcode, body):
        super()._observe(opcode, body)
        try:
            if opcode == SMSG_ATTACKERSTATEUPDATE:
                # uint32 hitInfo, packed attacker, packed victim, uint32 total damage, ...
                attacker, p = unpack_guid(body, 4)
                _, p = unpack_guid(body, p)
                if attacker == self.guid:
                    self.hits.append(struct.unpack_from("<I", body, p)[0])
            elif opcode == SMSG_PERIODICAURALOG:
                # packed target, packed caster, uint32 spell, uint32 count, uint32 aura, uint32 damage, ...
                _, p = unpack_guid(body, 0)
                caster, p = unpack_guid(body, p)
                spell, _, aura, damage = struct.unpack_from("<IIII", body, p)
                if caster == self.guid and aura == SPELL_AURA_PERIODIC_DAMAGE:
                    self.ticks.append((spell, damage))
        except (IndexError, struct.error):
            pass


mode = sys.argv[1] if len(sys.argv) > 1 else ""
if mode not in ("forever", "vanilla"):
    sys.exit("usage: t214_forever_warrior.py forever|vanilla")
forever = mode == "forever"

realm = Realm()
results = Results()
w = realm.login("PLAYER214", "Foreverrage" if forever else "Vanillarage", race=2, cls=1, client_class=CombatClient)
me = w.guid
clear_backpack(realm, w)   # a run before filled it with the same weapons
level = int(realm.sql("characters", "SELECT level FROM characters WHERE guid = %d" % me) or 1)
if level < LEVEL:
    w.gm(".levelup %d" % (LEVEL - level), select=me)
for spell in (ONE_HAND_AXES, TWO_HAND_AXES):
    w.gm(".learn %d" % spell, select=me)
w.gm(".maxskill", select=me)
w.gm(".cheat god on", select=me)
w.gm(".cheat criticals off", select=me)
w.set_active_mover()

# A boar at the Warrior's place; the Warrior stands 2 yards west of where it is once frozen, facing it.
w.gm(".npc spawn add %d" % BOAR, wait=1.5)
low = int(realm.sql("mangos", "SELECT MAX(guid) FROM creature WHERE id = %d" % BOAR).split()[-1])
boar = (0xF130 << 48) | (BOAR << 24) | low
w.gm(".modify hp 1000000 1000000", select=boar)
w.gm(".aura %d" % FREEZE, select=boar)
stand_before(w, boar)


def equip(entry):
    """Adds the item and equips it from the backpack, as the client does on a right click."""
    w.gm(".additem %d" % entry, select=me)
    w.gm(".save", select=me)
    slot = realm.sql("characters", "SELECT slot FROM character_inventory WHERE guid = %d AND bag = 0 AND slot >= 23 "
                     "AND item_id = %d LIMIT 1" % (me, entry))
    if slot:
        w.send(CMSG_AUTOEQUIP_ITEM, struct.pack("<BB", BACKPACK, int(slot.split()[0])))
        w.pump(1)
    w.gm(".save", select=me)
    return realm.sql("characters", "SELECT item_id FROM character_inventory WHERE guid = %d AND bag = 0 AND slot = %d"
                     % (me, MAIN_HAND_SLOT)) == str(entry)


def rage():
    for line in w.gm(".unit statinfo", select=me, wait=1.0):
        m = re.match(r"Rage: (\d+)", line)
        if m:
            return int(m.group(1))
    return -1


def swing(seconds):
    """Swings at the boar. (hits with damage, rage gained in tenths, Forever rage log lines)."""
    w.gm(".modify rage 0", select=me)
    realm.mark("swing")
    start = len(w.hits)
    w.select(boar)
    w.send(CMSG_ATTACKSWING, struct.pack("<Q", boar))
    w.pump(seconds)
    w.send(CMSG_ATTACKSTOP)
    w.pump(1)
    lines = re.findall(r"\[Forever\] Warrior rage: \S+ \+(\d+) tenths \(weapon (\d+) ms, (one|two)-hand, (main|off) hand, "
                       r"(hit|crit)\)", realm.log())
    return [h for h in w.hits[start:] if h > 0], rage(), lines


def forever_tenths(speed, two_hand, crit):
    return int(speed / 1000.0 * (4.5 if two_hand else 3.46) * (2 if crit else 1) * 10 + 1e-4)


per_hit = {}
for entry, speed, two_hand in (LARGE_AXE, HAND_AXE):
    name = "%s (%.1f sec)" % ("the two-hand axe" if two_hand else "the one-hand axe", speed / 1000.0)
    results.check("%s is in the main hand" % name, equip(entry))
    hits, gained, lines = swing(SWING_SECONDS)
    results.check("%s hits the boar" % name, len(hits) >= 2, "%d hits" % len(hits))
    per_hit[two_hand] = gained / float(max(1, len(hits)))
    if forever:
        results.check("each hit of %s logs its Forever rage" % name, len(lines) == len(hits),
                      "%d lines, %d hits" % (len(lines), len(hits)))
        wrong = [l for l in lines if int(l[1]) != speed or (l[2] == "two") != two_hand or l[3] != "main" or
                 abs(int(l[0]) - forever_tenths(speed, two_hand, l[4] == "crit")) > 1]
        results.check("the rage of each hit of %s comes from its speed, not its damage" % name, lines and not wrong,
                      "wrong lines %r" % wrong)
        results.check("the Warrior has the rage of those hits (%s)" % name,
                      abs(gained - sum(int(l[0]) for l in lines)) <= 1,
                      "rage %d tenths, lines %r" % (gained, [l[0] for l in lines]))
    else:
        results.check("no Forever rage line with the rules off (%s)" % name, not lines, "%r" % lines[:3])
        results.check("a hit of %s gives the stock rage, far below the Forever value" % name,
                      0 < per_hit[two_hand] < 0.6 * forever_tenths(speed, two_hand, False),
                      "%.1f tenths per hit, Forever %d" % (per_hit[two_hand], forever_tenths(speed, two_hand, False)))
if forever:
    results.check("a hit with the slow axe gives more rage than one with the fast axe", per_hit[True] > per_hit[False],
                  "%.1f vs %.1f tenths" % (per_hit[True], per_hit[False]))

# DoT crits: a Rogue's Rend and Shadow Word: Pain, then the Warrior's Rend, all with always-crit.
r = realm.login("PLAYER214R", "Foreverrogue" if forever else "Vanillarogue", race=2, cls=4, client_class=CombatClient)
r.gm(".goname %s" % ("Foreverrage" if forever else "Vanillarage"), wait=3.0)
r.gm(".cheat god on", select=r.guid)
r.gm(".cheat criticals on", select=r.guid)
w.gm(".cheat criticals on", select=me)

realm.mark("rogue dots")
r.gm(".cast %d triggered" % REND_1, select=boar)
r.gm(".cast %d triggered" % SHADOW_WORD_PAIN_1, select=boar)
r.pump(10)
w.pump(1)
rogue_rend = [d for s, d in r.ticks if s == REND_1]
rogue_pain = [d for s, d in r.ticks if s == SHADOW_WORD_PAIN_1]
rogue_crits = re.findall(r"\[Forever\] Warrior DoT crit: Foreverrogue", realm.log())
results.check("the Rogue's Rend ticks", len(rogue_rend) >= 2, "%r" % rogue_rend)
results.check("the Rogue's Shadow Word: Pain ticks", len(rogue_pain) >= 2, "%r" % rogue_pain)
results.check("no tick of the Rogue crits (another class's DoT)",
              not rogue_crits and len(set(rogue_rend)) == 1 and len(set(rogue_pain)) == 1,
              "Rend %r, Pain %r, crit lines %d" % (rogue_rend, rogue_pain, len(rogue_crits)))

realm.mark("warrior rend")
w.gm(".cast %d triggered" % REND_1, select=boar)
w.pump(10)
r.pump(1)
warrior_rend = [d for s, d in w.ticks if s == REND_1]
crit_lines = re.findall(r"\[Forever\] Warrior DoT crit: \S+ spell %d " % REND_1, realm.log())
results.check("the Warrior's Rend ticks", len(warrior_rend) >= 2, "%r" % warrior_rend)
base = rogue_rend[0] if rogue_rend else 0
if forever:
    results.check("each Rend tick of the Warrior crits: twice the Rogue's tick",
                  warrior_rend and all(d == 2 * base for d in warrior_rend), "Warrior %r, Rogue %d" % (warrior_rend, base))
    results.check("each crit tick logs a Forever line", len(crit_lines) == len(warrior_rend),
                  "%d lines, %d ticks" % (len(crit_lines), len(warrior_rend)))
else:
    results.check("the Warrior's Rend ticks are stock: the same as the Rogue's",
                  warrior_rend and all(d == base for d in warrior_rend), "Warrior %r, Rogue %d" % (warrior_rend, base))
    results.check("no Forever DoT crit line with the rules off", not crit_lines, "%d lines" % len(crit_lines))

# Deep Wounds: each melee crit applies Deep Wound; its ticks crit under the Forever rules.
w.gm(".learn %d" % DEEP_WOUNDS_3, select=me)
realm.mark("deep wounds")
start = len(w.ticks)
w.select(boar)
w.send(CMSG_ATTACKSWING, struct.pack("<Q", boar))
w.pump(SWING_SECONDS)
w.send(CMSG_ATTACKSTOP)
# Each crit applies Deep Wound again, which restarts its 3 sec tick timer: while the Warrior swings every
# 2 sec it hardly ticks. After the last swing it ticks every 3 sec: wait for two ticks.
w.pump(8)
deep = [d for s, d in w.ticks[start:] if s == DEEP_WOUND]
deep_lines = re.findall(r"\[Forever\] Warrior DoT crit: \S+ spell %d " % DEEP_WOUND, realm.log())
results.check("Deep Wound ticks on the boar", len(deep) >= 2, "%r" % deep)
if forever:
    results.check("each Deep Wound tick of the Warrior crits", len(deep_lines) == len(deep),
                  "%d crit lines, %d ticks" % (len(deep_lines), len(deep)))
else:
    results.check("no Deep Wound crit with the rules off", not deep_lines, "%d lines" % len(deep_lines))

w.gm(".cheat criticals off", select=me)
r.gm(".cheat criticals off", select=r.guid)
w.gm(".npc spawn delete", select=boar, wait=1.0)
results.finish()
