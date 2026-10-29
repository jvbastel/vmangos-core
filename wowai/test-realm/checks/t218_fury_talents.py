#!/usr/bin/env python3
"""In-game check of jvbastel/WOWAI#218 (spec #204): the Forever Fury talents that need server code.

    # A slot with the Forever rules:
    wowai/test-realm/realm.sh --ticket N acquire --forever
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t218_fury_talents.py forever
    # The same slot without them:
    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t218_fury_talents.py vanilla

A level 60 player Warrior learns the talents with `.learn` (the Forever-only spells only under the Forever
rules) and fights a frozen boar (GM stun 9454, a lot of health). A player Rogue deals damage to the Warrior
with `.damage`. Each part:

Gore Drinker 2/2 (60116): `.cast <spell> triggered` of Enrage (12880), Berserker Rage (18499), Bloodrage
    (2687), Death Wish (12328) and Bloodthirst (23881, the talent rank) gives the buff 60132; Recklessness
    (1719) does not. Then, at half health, the next 3 swings at the boar each heal 1% of maximum health
    (SMSG_SPELLHEALLOG of spell 60132), and the buff is gone after them.
Bloodthirst (23881): its hit on the boar, and its movement speed aura on the Warrior (data + the #212 script).
Lingering Rage 5/5 (60110): when the Warrior drops out of combat (the boar dies) with 50 Rage, it gets the
    buff 60131 for 10 sec, and the Rage does not decay while it runs; then it decays.
Blood Craze 3/3 (16492): a hit of 25% of maximum health (no crit) casts its heal 16491; a hit of 10% does not.
Improved Berserker Rage 2/2 (20501): Berserker Rage removes Hamstring (1715, a slow) and Frost Nova (122,
    a root), which have no mechanic in the 1.12 data.
Furious Precision 3/3 (60113): two daggers at dagger skill 20 against a level 4 boar (defense 20): each
    hand misses 24% x 0.4 = 9.6% (dual wield, low level target). The talent's +10% takes the off hand to 0:
    in 2 minutes the off hand never misses, the main hand does.

forever: every part above, with its "[Forever]" log line.
vanilla: none of it. No buffs 60131/60132 (the spells do not exist), no Gore Drinker heal, the Rage decays
         right after combat, a 25% hit gives no Blood Craze (the stock talent needs a crit), Berserker Rage
         keeps the slows, the off hand misses too. No "[Forever]" Fury line.
"""
import re
import struct
import sys
import time

from kit import Realm, Results, pos, stat
from wowclient import MSG_MOVE_HEARTBEAT, WorldClient, unpack_guid

CMSG_AUTOEQUIP_ITEM = 266
CMSG_ATTACKSWING = 321
CMSG_ATTACKSTOP = 322
SMSG_ATTACKERSTATEUPDATE = 330
SMSG_SPELLHEALLOG = 336
HITINFO_LEFTSWING, HITINFO_MISS = 0x4, 0x10

LEVEL = 60
BOAR = 3098            # Mottled Boar (Durotar), a neutral beast that can be attacked
FREEZE = 9454          # GM stun without an end
WORN_DAGGER = 2092     # 1.6 sec
DAGGERS, DUAL_WIELD = 1180, 674
DAGGER_SKILL = 173
BACKPACK = 255
MAIN_HAND_SLOT, OFF_HAND_SLOT = 15, 16

LINGERING_RAGE_5, GORE_DRINKER_2, FURIOUS_PRECISION_3 = 60110, 60116, 60113
BLOOD_CRAZE_3, BLOOD_CRAZE_HEAL_3 = 16492, 16491
IMPROVED_BERSERKER_RAGE_2 = 20501
LINGERING_RAGE_BUFF, GORE_DRINKER_BUFF = 60131, 60132
ENRAGE_BUFF, BERSERKER_RAGE, BLOODRAGE, BLOODRAGE_OVER_TIME, DEATH_WISH, BLOODTHIRST = 12880, 18499, 2687, 29131, 12328, 23881
RECKLESSNESS = 1719
HAMSTRING, FROST_NOVA = 1715, 122
PRECISION_SECONDS = 120


class FuryClient(WorldClient):
    """Keeps its own melee swings (hit info, damage) and the heals on itself (spell, amount)."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.swings = []
        self.heals = []

    def _observe(self, opcode, body):
        super()._observe(opcode, body)
        try:
            if opcode == SMSG_ATTACKERSTATEUPDATE:
                # uint32 hitInfo, packed attacker, packed victim, uint32 total damage, ...
                hit_info = struct.unpack_from("<I", body, 0)[0]
                attacker, p = unpack_guid(body, 4)
                _, p = unpack_guid(body, p)
                if attacker == self.guid:
                    self.swings.append((hit_info, struct.unpack_from("<I", body, p)[0]))
            elif opcode == SMSG_SPELLHEALLOG:
                # packed target, packed healer, uint32 spell, uint32 amount, uint8 crit
                target, p = unpack_guid(body, 0)
                _, p = unpack_guid(body, p)
                spell, amount = struct.unpack_from("<II", body, p)
                if target == self.guid:
                    self.heals.append((spell, amount))
        except (IndexError, struct.error):
            pass


mode = sys.argv[1] if len(sys.argv) > 1 else ""
if mode not in ("forever", "vanilla"):
    sys.exit("usage: t218_fury_talents.py forever|vanilla")
forever = mode == "forever"
NAME = "Foreverfury" if forever else "Vanillafury"

realm = Realm()
results = Results()
w = realm.login("PLAYER218", NAME, race=2, cls=1, client_class=FuryClient)
me = w.guid
level = int(realm.sql("characters", "SELECT level FROM characters WHERE guid = %d" % me) or 1)
if level < LEVEL:
    w.gm(".levelup %d" % (LEVEL - level), select=me)
for spell in (DAGGERS, DUAL_WIELD):
    w.gm(".learn %d" % spell, select=me)
w.gm(".maxskill", select=me)
w.gm(".cheat god off", select=me)
w.gm(".cheat criticals off", select=me)
w.set_active_mover()


def auras(guid=None):
    """{spell id: max duration ms} of the auras on the unit, from `.list auras`."""
    found = {}
    for line in w.gm(".list auras", select=guid or me, wait=0.7):
        m = re.search(r"\bid: (\d+) eff: \d+ type: \d+ duration: -?\d+ maxduration: (-?\d+)", line)
        if m:
            found[int(m.group(1))] = int(m.group(2))
    return found


def rage():
    for line in w.gm(".unit statinfo", select=me, wait=1.0):
        m = re.match(r"Rage: (\d+)", line)
        if m:
            return int(m.group(1))
    return -1


def equip(entry, slot):
    """Adds the item and equips it from the backpack, as the client does on a right click."""
    w.gm(".additem %d" % entry, select=me)
    w.gm(".save", select=me)
    bag_slot = realm.sql("characters", "SELECT slot FROM character_inventory WHERE guid = %d AND bag = 0 AND slot >= 23 "
                         "AND item_id = %d LIMIT 1" % (me, entry))
    if bag_slot:
        w.send(CMSG_AUTOEQUIP_ITEM, struct.pack("<BB", BACKPACK, int(bag_slot.split()[0])))
        w.pump(1)
    w.gm(".save", select=me)
    return realm.sql("characters", "SELECT item_id FROM character_inventory WHERE guid = %d AND bag = 0 AND slot = %d"
                     % (me, slot)) == str(entry)


def spawn_boar(level=None):
    """A frozen boar 2 yards east of the Warrior, which faces it."""
    here = pos(w, me)
    w.gm(".npc spawn add %d" % BOAR, wait=1.5)
    low = int(realm.sql("mangos", "SELECT MAX(guid) FROM creature WHERE id = %d" % BOAR).split()[-1])
    boar = (0xF130 << 48) | (BOAR << 24) | low
    if level:
        w.gm(".npc set level %d" % level, select=boar)
    w.gm(".modify hp 1000000 1000000", select=boar)
    w.gm(".aura %d" % FREEZE, select=boar)
    w.move(MSG_MOVE_HEARTBEAT, here[0] - 2.0, here[1], here[2], 0.0)
    w.pump(1)
    return boar


def swing(boar, seconds):
    start = len(w.swings)
    w.select(boar)
    w.send(CMSG_ATTACKSWING, struct.pack("<Q", boar))
    w.pump(seconds)
    w.send(CMSG_ATTACKSTOP)
    w.pump(1)
    return w.swings[start:]


def fury_lines():
    return re.findall(r"\[Forever\] (?:Lingering Rage|Gore Drinker|Improved Berserker Rage|Blood Craze): %s\b.*" % NAME,
                      realm.log())


# The talents. The Forever-only spells exist only under the Forever rules.
talents = [BLOOD_CRAZE_3, IMPROVED_BERSERKER_RAGE_2]
if forever:
    talents += [LINGERING_RAGE_5, GORE_DRINKER_2, FURIOUS_PRECISION_3]
for spell in talents:
    w.gm(".learn %d" % spell, select=me)
have = auras()
results.check("the Warrior has the talents %s" % talents, all(t in have for t in talents),
              "missing %r" % [t for t in talents if t not in have])
# The first dagger goes to the main hand, the second to the off hand (the Warrior can dual wield).
results.check("the main hand holds a dagger", equip(WORN_DAGGER, MAIN_HAND_SLOT))
results.check("the off hand holds a dagger", equip(WORN_DAGGER, OFF_HAND_SLOT))
boar = spawn_boar()

# Gore Drinker: the trigger spells give the buff, another spell does not.
realm.mark("gore drinker triggers")
for spell, target in ((ENRAGE_BUFF, me), (BERSERKER_RAGE, me), (BLOODRAGE, me), (DEATH_WISH, me),
                      (BLOODTHIRST, boar), (RECKLESSNESS, me)):
    w.gm(".unaura %d" % GORE_DRINKER_BUFF, select=me)
    w.gm(".cast %d triggered" % spell, select=target, wait=1.0)
    buffed = GORE_DRINKER_BUFF in auras()
    if forever and spell != RECKLESSNESS:
        results.check("a cast of %d gives the Gore Drinker buff" % spell, buffed)
    else:
        results.check("a cast of %d gives no Gore Drinker buff" % spell, not buffed)
    if spell == BLOODTHIRST:
        results.check("Bloodthirst hits the boar", 0 < stat(w, boar)[0] < 1000000, "boar health %d" % stat(w, boar)[0])
        results.check("Bloodthirst gives the Warrior its movement speed aura" if forever else
                      "stock Bloodthirst gives no movement speed aura", (BLOODTHIRST in auras()) == forever)
for spell in (ENRAGE_BUFF, BERSERKER_RAGE, BLOODRAGE_OVER_TIME, DEATH_WISH, BLOODTHIRST, RECKLESSNESS, GORE_DRINKER_BUFF):
    w.gm(".unaura %d" % spell, select=me, wait=0.5)
if forever:
    results.check("each Gore Drinker buff logs a Forever line",
                  len([l for l in fury_lines() if "Gore Drinker" in l]) == 5, "%r" % fury_lines())

# Gore Drinker: the next 3 melee attacks heal 1% of maximum health each (rank 2).
w.gm(".cast %d triggered" % BLOODRAGE, select=me, wait=1.0)
w.gm(".unaura %d" % BLOODRAGE_OVER_TIME, select=me)
top = stat(w, me)[1]
w.gm(".modify hp %d" % (top // 2), select=me)
heals_start = len(w.heals)
hits = [s for s in swing(boar, 8) if not s[0] & HITINFO_MISS and s[1] > 0]
gore = [a for s, a in w.heals[heals_start:] if s == GORE_DRINKER_BUFF]
results.check("the Warrior hits the boar more than 3 times", len(hits) > 3, "%d hits" % len(hits))
if forever:
    results.check("3 melee attacks each heal 1% of maximum health", gore == [top * 10 // 1000] * 3,
                  "heals %r, max health %d" % (gore, top))
    results.check("the Gore Drinker buff is gone after 3 attacks", GORE_DRINKER_BUFF not in auras())
else:
    results.check("no Gore Drinker heal with the rules off", not gore, "%r" % gore)

# Lingering Rage: the boar dies, the Warrior drops out of combat with 50 Rage.
w.gm(".modify rage 50", select=me)
realm.mark("lingering rage")
w.gm(".die", select=boar, wait=1.0)
if forever:
    dropped = realm.wait_log(r"\[Forever\] Lingering Rage: %s keeps its rage for 10000 ms\." % NAME, 15, w)
    results.check("out of combat, the Warrior gets Lingering Rage for 10 sec", dropped)
    results.check("the Lingering Rage buff lasts 10 sec", auras().get(LINGERING_RAGE_BUFF) == 10000, "%r" % auras())
    r0 = rage()
    w.pump(6)
    r1 = rage()
    results.check("the Rage does not decay while Lingering Rage runs", r0 > 0 and r1 == r0, "%d -> %d" % (r0, r1))
    w.pump(8)
    r2 = rage()
    results.check("the Rage decays after Lingering Rage", r2 < r1 and LINGERING_RAGE_BUFF not in auras(),
                  "%d -> %d" % (r1, r2))
else:
    r0 = rage()
    end, r = time.time() + 20, r0
    while time.time() < end and r >= r0:
        w.pump(2)
        r = rage()
    results.check("the Rage decays soon after combat", r < r0, "%d -> %d" % (r0, r))
    results.check("no Lingering Rage buff with the rules off", LINGERING_RAGE_BUFF not in auras())
w.gm(".npc spawn delete", select=boar, wait=1.0)

# Blood Craze: a Rogue deals 25%, then 10%, of the Warrior's maximum health.
rogue = realm.login("PLAYER218R", NAME + "r", race=2, cls=4)
rogue.gm(".goname %s" % NAME, wait=3.0)
w.gm(".modify hp %d" % top, select=me)
realm.mark("blood craze")
rogue.gm(".damage %d" % (top // 4), select=me, wait=1.0)
w.pump(1)
big = BLOOD_CRAZE_HEAL_3 in auras()
results.check("a hit of 25%% of maximum health %s Blood Craze" % ("triggers" if forever else "(no crit) does not trigger"),
              big == forever)
w.gm(".unaura %d" % BLOOD_CRAZE_HEAL_3, select=me)
w.gm(".modify hp %d" % top, select=me)
rogue.gm(".damage %d" % (top // 10), select=me, wait=1.0)
w.pump(1)
results.check("a hit of 10% of maximum health does not trigger Blood Craze", BLOOD_CRAZE_HEAL_3 not in auras())
if forever:
    results.check("the Blood Craze trigger logs one Forever line", len([l for l in fury_lines() if "Blood Craze" in l]) == 1,
                  "%r" % fury_lines())

# Improved Berserker Rage 2/2: Berserker Rage removes a slow and a root.
w.gm(".aura %d" % HAMSTRING, select=me)
w.gm(".aura %d" % FROST_NOVA, select=me)
before = auras()
results.check("the Warrior is slowed and rooted", HAMSTRING in before and FROST_NOVA in before, "%r" % sorted(before))
realm.mark("improved berserker rage")
w.gm(".cast %d triggered" % BERSERKER_RAGE, select=me, wait=1.0)
after = auras()
if forever:
    results.check("Berserker Rage removes the slow and the root", HAMSTRING not in after and FROST_NOVA not in after,
                  "%r" % sorted(after))
    results.check("the removal logs a Forever line", any("Improved Berserker Rage" in l for l in fury_lines()))
else:
    results.check("stock Berserker Rage keeps the slow and the root", HAMSTRING in after and FROST_NOVA in after,
                  "%r" % sorted(after))
for spell in (HAMSTRING, FROST_NOVA, BERSERKER_RAGE, GORE_DRINKER_BUFF):
    w.gm(".unaura %d" % spell, select=me, wait=0.5)

# Furious Precision: two daggers at skill 20 against a level 4 boar.
w.gm(".setskill %d 20 300" % DAGGER_SKILL, select=me)
boar = spawn_boar(level=4)
swings = swing(boar, PRECISION_SECONDS)
main = [h for h, _ in swings if not h & HITINFO_LEFTSWING]
off = [h for h, _ in swings if h & HITINFO_LEFTSWING]
main_misses = len([h for h in main if h & HITINFO_MISS])
off_misses = len([h for h in off if h & HITINFO_MISS])
detail = "main hand %d misses of %d, off hand %d of %d" % (main_misses, len(main), off_misses, len(off))
results.check("both hands swing often", len(main) >= 50 and len(off) >= 50, detail)
results.check("the main hand misses (9.6%)", main_misses > 0, detail)
if forever:
    results.check("with Furious Precision 3/3 the off hand never misses", off_misses == 0, detail)
else:
    results.check("without Furious Precision the off hand misses too", off_misses > 0, detail)

if not forever:
    results.check("no Forever Fury line with the rules off", not re.search(
        r"\[Forever\] (?:Lingering Rage|Gore Drinker|Improved Berserker Rage|Blood Craze): %s\b" % NAME,
        realm.log(all_lines=True)))

w.gm(".npc spawn delete", select=boar, wait=1.0)
results.finish()
