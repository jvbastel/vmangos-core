#!/usr/bin/env python3
"""In-game check of jvbastel/WOWAI#217 (spec #204): the C++ talents of the Forever Arms tree.

    # A slot with the Forever rules:
    wowai/test-realm/realm.sh --ticket N acquire --forever
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t217_forever_arms.py forever
    # The same slot without them:
    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t217_forever_arms.py vanilla

A level 60 player Warrior learns the talent spells by ID (.learn): Improved Tactical Mastery rank 1 (12295),
Bloodthrill 5/5 (60105), Weaponmaster 5/5 (12815) and Improved Slam 2/2 (12330), plus the baseline Tactical
Mastery (60125). The same IDs are stock Tactical Mastery 1/5, Sword Specialization 5/5 and Improved Slam 2/5
in vanilla; 60105 and 60125 do not exist there. It fights frozen targets (GM stun 9454, a lot of health).
`.cheat cooldown on` and `.cheat powercost on` remove cooldowns and Rage costs, `.cheat procs on` makes
each proc happen (Bloodthrill too).

  Improved Tactical Mastery: 60 Rage, then Defensive Stance. forever: 13 Rage stay (10 + 3);
      vanilla: 5 (stock Tactical Mastery 1/5).
  Bloodthrill: a one-hand axe swings at a boar. Without Rend no Bloodthrill line and Overpower (7384) is
      refused (a frozen boar cannot dodge). With the Warrior's Rend (772): forever, a "[Forever]
      Bloodthrill" line, then Overpower casts; vanilla, no line and Overpower stays refused.
  Weaponmaster: forever, the axe swings log "+5%" crit lines; a mace logs "ignores 15%" of the boar's
      armor; with a sword and a shield, Shield Bash (72) logs "no sword proc" and gives no extra attack.
      vanilla: no "[Forever] Weaponmaster" line.
  Spearing Strike (60100): forever, against a Dragonkin, a Giant and a mounted Humanoid the damage is x3
      (log line), the mounted one is dismounted; against the boar (a Beast) no bonus line. vanilla: the
      spell does not exist, no line.
  Improved Slam: a 2.8 sec two-hand axe swings; Slam (1464) is cast 0.3 sec after a swing, and again
      2.0 sec after a swing (its cast then spans the next swing). forever: the next swing comes one weapon
      speed after the last one (the cast neither stops nor resets the swing); vanilla: it comes at least
      one weapon speed after the end of the cast.
"""
import re
import struct
import sys
import time

from kit import Realm, Results, pos
from wowclient import MSG_MOVE_HEARTBEAT, WorldClient, unpack_guid

CMSG_AUTOEQUIP_ITEM = 266
CMSG_CAST_SPELL = 302
SMSG_CAST_RESULT = 304
SMSG_SPELL_GO = 306
CMSG_ATTACKSWING = 321
CMSG_ATTACKSTOP = 322
SMSG_ATTACKERSTATEUPDATE = 330
TARGET_FLAG_UNIT = 0x2

LEVEL = 60
BOAR = 3098            # Mottled Boar (Durotar), a neutral Beast that can be attacked
FREEZE = 9454          # GM stun without an end
BACKPACK = 255
MAIN_HAND_SLOT, OFF_HAND_SLOT = 15, 16
# Weapon skills: one-hand and two-hand axes, one-hand maces, one-hand swords.
SKILL_SPELLS = (196, 197, 198, 201)
HAND_AXE = 2134        # one-hand axe, 2.0 sec
LARGE_AXE = 2491       # two-hand axe, 2.8 sec
LARGE_AXE_SPEED = 2.8

BATTLE_STANCE, DEFENSIVE_STANCE = 2457, 71
REND_1, OVERPOWER_1, SLAM_1464, SHIELD_BASH_1 = 772, 7384, 1464, 72
TACTICAL_MASTERY = 60125            # Forever baseline (10 Rage)
IMPROVED_TACTICAL_MASTERY_1 = 12295
BLOODTHRILL_5 = 60105
WEAPONMASTER_5 = 12815
IMPROVED_SLAM_2 = 12330
SPEARING_STRIKE = 60100
CREATURE_BEAST, CREATURE_DRAGONKIN, CREATURE_GIANT, CREATURE_HUMANOID = 1, 2, 5, 7
ATTACKABLE_FACTIONS = "(7, 14, 16, 22, 24, 48, 54)"


class ArmsClient(WorldClient):
    """Keeps the times of its own melee hits, its SPELL_GO spells and its cast results."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.hits = []          # (time, damage)
        self.spell_go = []      # (time, spell)
        self.cast_results = []  # (spell, reason or None)

    def _observe(self, opcode, body):
        super()._observe(opcode, body)
        try:
            if opcode == SMSG_ATTACKERSTATEUPDATE:
                attacker, p = unpack_guid(body, 4)
                _, p = unpack_guid(body, p)
                if attacker == self.guid:
                    self.hits.append((time.time(), struct.unpack_from("<I", body, p)[0]))
            elif opcode == SMSG_SPELL_GO:
                caster, p = unpack_guid(body, 0)
                _, p = unpack_guid(body, p)
                if caster == self.guid:
                    self.spell_go.append((time.time(), struct.unpack_from("<I", body, p)[0]))
            elif opcode == SMSG_CAST_RESULT:
                spell, status = struct.unpack_from("<IB", body, 0)
                self.cast_results.append((spell, body[5] if status == 2 and len(body) > 5 else None))
        except (IndexError, struct.error):
            pass

    def cast_at(self, spell, target):
        """Sends CMSG_CAST_SPELL at a unit, as the action bar does. Does not wait."""
        mask, packed = 0, b""
        for i in range(8):
            byte = (target >> (8 * i)) & 0xFF
            if byte:
                mask |= 1 << i
                packed += bytes([byte])
        self.send(CMSG_CAST_SPELL, struct.pack("<I", spell) + struct.pack("<H", TARGET_FLAG_UNIT) + bytes([mask]) + packed)

    def cast_went(self, spell, target, seconds=1.5):
        """Casts and waits; True when a SPELL_GO of the spell came."""
        start = len(self.spell_go)
        self.cast_at(spell, target)
        self.pump(seconds)
        return any(s == spell for _, s in self.spell_go[start:])

    def wait_hit(self, seconds=6.0):
        """Waits for the next own melee hit; its time, or None."""
        start = len(self.hits)
        end = time.time() + seconds
        while time.time() < end and len(self.hits) == start:
            self.pump(0.05)
        return self.hits[start][0] if len(self.hits) > start else None


mode = sys.argv[1] if len(sys.argv) > 1 else ""
if mode not in ("forever", "vanilla"):
    sys.exit("usage: t217_forever_arms.py forever|vanilla")
forever = mode == "forever"

realm = Realm()
results = Results()
w = realm.login("PLAYER217", "Foreverarms" if forever else "Vanillaarms", race=2, cls=1, client_class=ArmsClient)
me = w.guid
level = int(realm.sql("characters", "SELECT level FROM characters WHERE guid = %d" % me) or 1)
if level < LEVEL:
    w.gm(".levelup %d" % (LEVEL - level), select=me)
for spell in SKILL_SPELLS + (REND_1, OVERPOWER_1, SLAM_1464, SHIELD_BASH_1, DEFENSIVE_STANCE):
    w.gm(".learn %d" % spell, select=me)
w.gm(".maxskill", select=me)
w.gm(".cheat god on", select=me)
w.gm(".cheat cooldown on", select=me)
w.gm(".cheat powercost on", select=me)
w.gm(".cheat criticals off", select=me)
w.gm(".cheat procs off", select=me)
talents = (TACTICAL_MASTERY, IMPROVED_TACTICAL_MASTERY_1, BLOODTHRILL_5, WEAPONMASTER_5, IMPROVED_SLAM_2)
for spell in talents:
    w.gm(".learn %d" % spell, select=me)
w.set_active_mover()
here = pos(w, me)


def spawn(entry):
    """Spawns a creature at the Warrior's place, gives it a lot of health and freezes it. Its guid."""
    w.gm(".npc spawn add %d" % entry, wait=1.5)
    low = int(realm.sql("mangos", "SELECT MAX(guid) FROM creature WHERE id = %d" % entry).split()[-1])
    guid = (0xF130 << 48) | (entry << 24) | low
    w.gm(".modify hp 1000000 1000000", select=guid)
    w.gm(".aura %d" % FREEZE, select=guid)
    return guid


def despawn(guid):
    w.gm(".npc spawn delete", select=guid, wait=1.0)


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
    worn = realm.sql("characters", "SELECT item_id FROM character_inventory WHERE guid = %d AND bag = 0 AND slot IN (%d, %d)"
                     % (me, MAIN_HAND_SLOT, OFF_HAND_SLOT)).split()
    return str(entry) in worn


def first_entry(query):
    rows = realm.sql("mangos", query).split()
    return int(rows[0]) if rows else 0


def rage():
    for line in w.gm(".unit statinfo", select=me, wait=1.0):
        m = re.match(r"Rage: (\d+)", line)
        if m:
            return int(m.group(1))
    return -1


def start_swing(target):
    w.select(target)
    w.send(CMSG_ATTACKSWING, struct.pack("<Q", target))


def stop_swing():
    w.send(CMSG_ATTACKSTOP)
    w.pump(1)


boar = spawn(BOAR)
w.move(MSG_MOVE_HEARTBEAT, here[0] - 2.0, here[1], here[2], 0.0)
w.pump(1)

# Improved Tactical Mastery ---------------------------------------------------------------------------
w.gm(".cast %d triggered" % BATTLE_STANCE, select=me)
w.gm(".modify rage 60", select=me)
before = rage()
realm.mark("t217 stance")
w.gm(".cast %d triggered" % DEFENSIVE_STANCE, select=me)
w.pump(1)
after = rage()
kept = 130 if forever else 50
results.check("the Warrior has Rage before the stance change", before >= 300, "%d tenths" % before)
results.check("the stance change keeps %d Rage (%s)" % (kept // 10, "10 + Improved Tactical Mastery 1/5 = 3"
              if forever else "stock Tactical Mastery 1/5"), after == min(before, kept), "%d -> %d tenths" % (before, after))
stance_lines = re.findall(r"\[Forever\] Stance change: \S+ keeps (\d+) of (\d+) Rage tenths", realm.log())
if forever:
    results.check("the stance change logs its Forever line", stance_lines and stance_lines[-1][0] == str(kept),
                  "%r" % stance_lines)
else:
    results.check("no Forever stance line with the rules off", not stance_lines, "%r" % stance_lines)
w.gm(".cast %d triggered" % BATTLE_STANCE, select=me)
w.pump(1)

# Bloodthrill -------------------------------------------------------------------------------------------
results.check("the one-hand axe is in the main hand", equip(HAND_AXE))
w.gm(".cheat procs on", select=me)
realm.mark("t217 no rend")
start_swing(boar)
w.pump(5)
results.check("without Rend there is no Bloodthrill line", "Bloodthrill" not in realm.log())
results.check("without Rend Overpower is refused (the frozen boar does not dodge)",
              not w.cast_went(OVERPOWER_1, boar))
realm.mark("t217 rend")
w.gm(".cast %d triggered" % REND_1, select=boar)
proc = realm.wait_log(r"\[Forever\] Bloodthrill: ", 6, client=w) if forever else None
if forever:
    results.check("a main hand hit on the Rend target logs Bloodthrill (procs on)", bool(proc))
    results.check("after Bloodthrill Overpower casts", w.cast_went(OVERPOWER_1, boar))
else:
    w.pump(6)
    results.check("no Bloodthrill line with the rules off", "Bloodthrill" not in realm.log())
    results.check("with Rend Overpower stays refused with the rules off", not w.cast_went(OVERPOWER_1, boar))
w.gm(".cheat procs off", select=me)

# Weaponmaster: axe crit and mace armor ignore ----------------------------------------------------------
realm.mark("t217 axe")
w.pump(4)
crit_lines = re.findall(r"\[Forever\] Weaponmaster crit: \S+ \+(\d+)% \((main|off) hand\)", realm.log())
if forever:
    results.check("the axe swings get +5% crit from Weaponmaster 5/5",
                  crit_lines and all(c == ("5", "main") for c in crit_lines), "%r" % crit_lines[:3])
else:
    results.check("no Forever Weaponmaster crit line with the rules off", not crit_lines, "%r" % crit_lines[:3])
stop_swing()

mace = first_entry("SELECT entry FROM item_template WHERE class = 2 AND subclass = 4 AND inventory_type IN (13, 21) "
                   "AND required_level <= 1 ORDER BY entry LIMIT 1")
results.check("a one-hand mace is in the main hand", mace and equip(mace), "entry %d" % mace)
realm.mark("t217 mace")
start_swing(boar)
w.pump(5)
stop_swing()
armor_lines = re.findall(r"\[Forever\] Weaponmaster armor: \S+ ignores (\d+)%, (\d+) -> (\d+)", realm.log())
if forever:
    results.check("the mace ignores 15% of the boar's armor",
                  armor_lines and all(a[0] == "15" and abs(int(a[2]) - int(a[1]) * 0.85) <= 1 for a in armor_lines),
                  "%r" % armor_lines[:3])
else:
    results.check("no Forever Weaponmaster armor line with the rules off", not armor_lines, "%r" % armor_lines[:3])

# Weaponmaster: no sword proc from Shield Bash ----------------------------------------------------------
sword = first_entry("SELECT entry FROM item_template WHERE class = 2 AND subclass = 7 AND inventory_type IN (13, 21) "
                    "AND required_level <= 1 ORDER BY delay DESC, entry LIMIT 1")
shield = first_entry("SELECT entry FROM item_template WHERE class = 4 AND subclass = 6 AND required_level <= 1 "
                     "ORDER BY entry LIMIT 1")
results.check("a one-hand sword and a shield are worn", sword and shield and equip(sword) and equip(shield),
              "sword %d, shield %d" % (sword, shield))
w.gm(".cheat procs on", select=me)
realm.mark("t217 shield bash")
start_swing(boar)
w.wait_hit()
w.pump(0.8)          # the extra attack of this hit's sword proc comes and goes
hits_before = len(w.hits)
bash = w.cast_went(SHIELD_BASH_1, boar, seconds=0.8)
extra = len(w.hits) - hits_before
stop_swing()
w.gm(".cheat procs off", select=me)
results.check("Shield Bash casts", bash)
no_proc = re.findall(r"\[Forever\] Weaponmaster: no sword proc from spell (\d+)", realm.log())
if forever:
    results.check("Shield Bash gives no Weaponmaster extra attack", extra == 0 and str(SHIELD_BASH_1) in no_proc,
                  "%d hits after the bash, lines %r" % (extra, no_proc))
else:
    results.check("no Forever Weaponmaster line with the rules off", not no_proc, "%r" % no_proc)
    print("INFO: vanilla, %d melee hits within 0.8 sec after Shield Bash (stock sword proc)" % extra)

# Spearing Strike --------------------------------------------------------------------------------------
targets = [("the boar (a Beast)", BOAR, False)]
for label, query in (
        ("a Dragonkin", "type = %d" % CREATURE_DRAGONKIN),
        ("a Giant", "type = %d" % CREATURE_GIANT),
        ("a mounted Humanoid", "type = %d AND mount_display_id > 0" % CREATURE_HUMANOID)):
    entry = first_entry("SELECT entry FROM creature_template WHERE %s AND faction IN %s AND level_max <= 63 "
                        "ORDER BY entry LIMIT 1" % (query, ATTACKABLE_FACTIONS))
    results.check("the world has %s to spawn" % label, entry > 0)
    if entry:
        targets.append((label, entry, True))
equip(LARGE_AXE)
for label, entry, bonus in targets:
    guid = boar if entry == BOAR else spawn(entry)
    realm.mark("t217 spearing %d" % entry)
    w.gm(".cast %d triggered" % SPEARING_STRIKE, select=guid)
    w.pump(1)
    lines = re.findall(r"\[Forever\] Spearing Strike: \S+ on \S+ \(creature type (\d+)(, dismounted)?\), "
                       r"([\d.]+) -> ([\d.]+)", realm.log())
    if forever and bonus:
        results.check("Spearing Strike deals x3 to %s" % label,
                      len(lines) == 1 and abs(float(lines[0][3]) - 3 * float(lines[0][2])) < 0.2, "%r" % lines)
        if "mounted" in label:
            results.check("Spearing Strike dismounts %s" % label, lines and lines[0][1] == ", dismounted", "%r" % lines)
    else:
        results.check("no Spearing Strike bonus line for %s" % label, not lines, "%r" % lines)
    if guid != boar:
        despawn(guid)

# Improved Slam ----------------------------------------------------------------------------------------
results.check("the two-hand axe is in the main hand", equip(LARGE_AXE))
start_swing(boar)
w.wait_hit()
for label, delay in (("Slam cast 0.3 sec after a swing", 0.3), ("Slam cast across the next swing", 2.0)):
    last = w.wait_hit()
    w.pump(delay)
    went_at = len(w.spell_go)
    w.cast_at(SLAM_1464, boar)
    following = w.wait_hit(8.0)
    w.pump(0.5)
    slam = [t for t, s in w.spell_go[went_at:] if s == SLAM_1464]
    gap = (following - last) if (following and last) else -1.0
    results.check("%s: Slam casts" % label, bool(slam))
    if forever:
        results.check("%s: the next swing comes one weapon speed after the last" % label,
                      0 < gap <= LARGE_AXE_SPEED + 0.6, "%.2f sec" % gap)
    else:
        results.check("%s: the cast stops or resets the swing (stock)" % label,
                      gap >= LARGE_AXE_SPEED + 1.0, "%.2f sec" % gap)
stop_swing()

w.gm(".cheat cooldown off", select=me)
w.gm(".cheat powercost off", select=me)
despawn(boar)
results.finish()
