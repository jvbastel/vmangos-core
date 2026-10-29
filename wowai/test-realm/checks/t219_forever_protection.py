#!/usr/bin/env python3
"""In-game check of jvbastel/WOWAI#219 (spec #204): the Protection talents with server code, on the check kit.

    wowai/test-realm/realm.sh --ticket N acquire --forever
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t219_forever_protection.py forever
    # The same slot without the Forever rules:
    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t219_forever_protection.py vanilla

A level 60 player Warrior with a one-hand axe learns Protection talents with CMSG_LEARN_TALENT, as the
talent window does. The check reads its auras with `.list auras`, its weapon damage with `.unit statinfo`,
the melee results of a boar from SMSG_ATTACKERSTATEUPDATE, its Rage gains from SMSG_SPELLENERGIZELOG, the
duration of a new aura from SMSG_UPDATE_AURA_DURATION, and the "[Forever]" lines of the server log.

forever: Shield Specialization 5/5, Iron Will 5/5, Master of Defense 2/2, Defiance 3/3, 10 more points, Bastion
         5/5 (30 points).
         Defiance and Bastion (shield gate): without a shield neither aura is on. A shield equipped from the
         bag: Bastion (16542) is on and the main hand damage is 10% higher; in Defensive Stance Defiance
         (12789) is on, in Battle Stance it is off. A login with the shield keeps Bastion (and Defiance in
         Defensive Stance). The shield back into the bag: both off, the damage is back.
         Master of Defense 2/2 (100%): a level 60 boar attacks the Warrior (GM Evasion for more dodges). With
         the shield each dodge and parry logs a Master of Defense line and gives 5 Rage (spell 23602).
         Without the shield the dodges and parries give no Rage from 23602 and no line.
         Iron Will 5/5: the boar casts Hammer of Justice rank 1 (853, 3 sec stun) and Fear rank 1 (5782, 10 sec)
         on the Warrior: they last 15% less (2.55 and 8.5 sec), with an Iron Will log line each.
vanilla: talent 60006 (Master of Defense) is refused. Stock Defiance 3/5 is on in Defensive Stance without a
         shield (no gate). Iron Will 5/5 (stock: resist chance) leaves the durations at 3 and 10 sec. No
         "[Forever]" line.
"""
import re
import struct
import sys

from kit import Realm, Results, kill, pos
from wowclient import MSG_MOVE_HEARTBEAT, WorldClient, unpack_guid

CMSG_AUTOEQUIP_ITEM = 266
CMSG_AUTOSTORE_BAG_ITEM = 267
CMSG_CAST_SPELL = 302
CMSG_ATTACKSWING = 321
CMSG_ATTACKSTOP = 322
SMSG_UPDATE_AURA_DURATION = 311
SMSG_ATTACKERSTATEUPDATE = 330
SMSG_SPELLENERGIZELOG = 337
SMSG_LEARNED_SPELL = 299
CMSG_LEARN_TALENT = 593
CMSG_LOGOUT_REQUEST = 75
SMSG_LOGOUT_COMPLETE = 77
CMSG_PLAYER_LOGIN = 61
SMSG_LOGIN_VERIFY_WORLD = 566
VICTIMSTATE_DODGE, VICTIMSTATE_PARRY = 2, 3
POWER_RAGE = 1

LEVEL = 60
ACCOUNT = "PLAYER219"
BOAR = 3098                 # Mottled Boar (Durotar), neutral
ONE_HAND_AXES, SHIELD_SKILL = 196, 9116
HAND_AXE = 2134
BACKPACK, OFF_HAND_SLOT = 255, 16
BATTLE_STANCE, DEFENSIVE_STANCE = 2457, 71
EVASION = 5277              # +50% dodge, 15 sec (GM aura, for more dodges)
HAMMER_OF_JUSTICE_1, FEAR_1 = 853, 5782     # 3 sec stun, 10 sec fear
RAGE_SPELL = 23602

SHIELD_SPEC, IRON_WILL, MASTER_OF_DEFENSE, DEFIANCE, BASTION = 1601, 141, 60006, 144, 702
# 30 Forever Protection points: (talent, ranks). Bastion (tier 6) needs 25 before it.
FOREVER_PATH = [(SHIELD_SPEC, 5), (IRON_WILL, 5), (MASTER_OF_DEFENSE, 2), (DEFIANCE, 3),
                (146, 3), (149, 2),             # Improved Sunder Armor, Improved Shield Bash: 20
                (150, 2), (152, 1), (60008, 2),  # Improved Shield Wall, Concussion Blow, Focused Rage: 25
                (BASTION, 5)]
# Vanilla: Shield Specialization 5/5 (tier 1), Iron Will 5/5 (tier 2), Defiance 3/5 (tier 3).
VANILLA_PATH = [(SHIELD_SPEC, 5), (IRON_WILL, 5), (DEFIANCE, 3)]
DEFIANCE_3, BASTION_5 = 12789, 16542
SWING_SECONDS = 15


class ProtectionClient(WorldClient):
    """Keeps the learned spells, the boar's melee results on the character, its Rage gains and the durations
    of its new auras."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.learned = []
        self.avoided = []      # victim states of melee swings at the character (dodge, parry, ...)
        self.energize = []     # (spell, amount) of Rage
        self.durations = []    # aura durations in ms
        self.attacker = 0

    def _observe(self, opcode, body):
        super()._observe(opcode, body)
        try:
            if opcode == SMSG_LEARNED_SPELL:
                self.learned.append(struct.unpack_from("<I", body, 0)[0])
            elif opcode == SMSG_ATTACKERSTATEUPDATE:
                # uint32 hitInfo, packed attacker, packed victim, int32 total, uint8 count, count x 20 bytes,
                # uint32 victim state, ...
                attacker, p = unpack_guid(body, 4)
                victim, p = unpack_guid(body, p)
                count = body[p + 4]
                state = struct.unpack_from("<I", body, p + 5 + 20 * count)[0]
                if victim == self.guid and attacker == self.attacker:
                    self.avoided.append(state)
            elif opcode == SMSG_SPELLENERGIZELOG:
                target, p = unpack_guid(body, 0)
                _, p = unpack_guid(body, p)
                spell, power, amount = struct.unpack_from("<III", body, p)
                if target == self.guid and power == POWER_RAGE:
                    self.energize.append((spell, amount))
            elif opcode == SMSG_UPDATE_AURA_DURATION:
                self.durations.append(struct.unpack_from("<BI", body, 0)[1])
        except (IndexError, struct.error):
            pass

    def learn_talent(self, talent, rank=0):
        start = len(self.learned)
        self.send(CMSG_LEARN_TALENT, struct.pack("<II", talent, rank))
        self.pump(1.5)
        return self.learned[start:]

    def cast_on_self(self, spell):
        self.send(CMSG_CAST_SPELL, struct.pack("<IH", spell, 0))
        self.pump(1.5)

    def logout(self):
        self.send(CMSG_LOGOUT_REQUEST)
        self.wait_for(SMSG_LOGOUT_COMPLETE, 30)
        self.pump(2)

    def login_again(self):
        self.send(CMSG_PLAYER_LOGIN, struct.pack("<Q", self.guid))
        self.wait_for(SMSG_LOGIN_VERIFY_WORLD, 30)
        self.pump(3)


mode = sys.argv[1] if len(sys.argv) > 1 else ""
if mode not in ("forever", "vanilla"):
    sys.exit("usage: t219_forever_protection.py forever|vanilla")
forever = mode == "forever"

realm = Realm()
results = Results()
name = "Foreverprot" if forever else "Vanillaprot"
c = realm.login(ACCOUNT, name, race=2, cls=1, client_class=ProtectionClient)
me = c.guid
level = int(realm.sql("characters", "SELECT level FROM characters WHERE guid = %d" % me) or 1)
if level < LEVEL:
    c.gm(".levelup %d" % (LEVEL - level), select=me)
for spell in (ONE_HAND_AXES, SHIELD_SKILL, DEFENSIVE_STANCE, BATTLE_STANCE):
    c.gm(".learn %d" % spell, select=me)
c.gm(".maxskill", select=me)
c.gm(".cheat god on", select=me)
c.gm(".reset talents", select=me)
c.pump(1)
c.set_active_mover()


def auras():
    """spell -> aura type of each aura on the character (`.list auras`)."""
    found = {}
    for line in c.gm(".list auras", select=me, wait=1.0):
        m = re.search(r"id: (\d+) eff: \d+ type: (\d+)", line)
        if m:
            found[int(m.group(1))] = int(m.group(2))
    return found


def min_damage():
    for line in c.gm(".unit statinfo", select=me, wait=1.0):
        m = re.match(r"Min damage: ([\d.]+)", line)
        if m:
            return float(m.group(1))
    return -1.0


def item_in(slot):
    c.gm(".save", select=me)
    row = realm.sql("characters", "SELECT item_id FROM character_inventory WHERE guid = %d AND bag = 0 AND slot = %d"
                    % (me, slot))
    return int(row) if row else 0


def equip(entry):
    """Adds the item and equips it from the backpack, as the client does on a right click."""
    c.gm(".additem %d" % entry, select=me)
    c.gm(".save", select=me)
    slot = realm.sql("characters", "SELECT slot FROM character_inventory WHERE guid = %d AND bag = 0 AND slot >= 23 "
                     "AND item_id = %d LIMIT 1" % (me, entry))
    if slot:
        c.send(CMSG_AUTOEQUIP_ITEM, struct.pack("<BB", BACKPACK, int(slot.split()[0])))
        c.pump(1.5)


def unequip_off_hand():
    """Puts the off-hand item into the backpack, as a drag to the bag does."""
    c.send(CMSG_AUTOSTORE_BAG_ITEM, struct.pack("<BBB", BACKPACK, OFF_HAND_SLOT, BACKPACK))
    c.pump(1.5)
    return item_in(OFF_HAND_SLOT) == 0


def stance(spell):
    c.cast_on_self(spell)
    return spell in auras()


def learn(path):
    return {talent: [s for rank in range(ranks) for s in c.learn_talent(talent, rank)] for talent, ranks in path}


shield = int(realm.sql("mangos", "SELECT DISTINCT entry FROM item_template WHERE class = 4 AND subclass = 6 "
                       "AND quality = 1 AND required_level BETWEEN 1 AND 10 ORDER BY entry LIMIT 1").split()[0])
unequip_off_hand()
equip(HAND_AXE)
results.check("a one-hand axe is in the main hand", item_in(15) == HAND_AXE)
realm.mark("t219 " + mode)

if not forever:
    learned = learn(VANILLA_PATH)
    results.check("vanilla: Shield Specialization 5/5, Iron Will 5/5 and Defiance 3/5 are learned",
                  all(len(learned[t]) == r for t, r in VANILLA_PATH), "learned %r" % learned)
    results.check("vanilla: talent 60006 (Master of Defense) is refused", c.learn_talent(MASTER_OF_DEFENSE) == [])
    results.check("vanilla: Defensive Stance", stance(DEFENSIVE_STANCE))
    results.check("vanilla: stock Defiance is on without a shield (no gate)", DEFIANCE_3 in auras(), "%r" % auras())
else:
    learned = learn(FOREVER_PATH)
    results.check("the 30 Protection points are learned", all(len(learned[t]) == r for t, r in FOREVER_PATH),
                  "learned %r" % learned)
    results.check("Master of Defense 2/2 and Bastion 5/5 are learned",
                  learned[MASTER_OF_DEFENSE] == [60117, 60118] and BASTION_5 in learned[BASTION], "%r" % learned)

    # Defiance and Bastion: the shield gate.
    results.check("Defensive Stance", stance(DEFENSIVE_STANCE))
    now = auras()
    results.check("no shield: Defiance and Bastion are off", DEFIANCE_3 not in now and BASTION_5 not in now,
                  "%r" % now)
    bare = min_damage()
    equip(shield)
    results.check("a shield is in the off hand", item_in(OFF_HAND_SLOT) == shield, "shield %d" % shield)
    now = auras()
    results.check("with the shield in Defensive Stance: Defiance (MOD_THREAT) and Bastion are on",
                  now.get(DEFIANCE_3) == 10 and now.get(BASTION_5) == 79, "%r" % now)
    shielded = min_damage()
    results.check("Bastion 5/5 raises the main hand damage by 10%", bare > 0 and abs(shielded / bare - 1.10) < 0.01,
                  "%.1f -> %.1f" % (bare, shielded))
    results.check("Battle Stance", stance(BATTLE_STANCE))
    now = auras()
    results.check("in Battle Stance: Defiance is off, Bastion stays on",
                  DEFIANCE_3 not in now and BASTION_5 in now, "%r" % now)
    stance(DEFENSIVE_STANCE)
    results.check("back in Defensive Stance: Defiance is on again", DEFIANCE_3 in auras())

    c.logout()
    c.login_again()
    now = auras()
    results.check("a login with the shield keeps Bastion", BASTION_5 in now, "%r" % now)
    if DEFENSIVE_STANCE in now:
        results.check("a login in Defensive Stance with the shield keeps Defiance", DEFIANCE_3 in now, "%r" % now)
    c.set_active_mover()

    results.check("the shield goes back into the bag", unequip_off_hand())
    now = auras()
    results.check("the shield off: Defiance and Bastion are off", DEFIANCE_3 not in now and BASTION_5 not in now,
                  "%r" % now)
    results.check("the shield off: the damage is back", abs(min_damage() - bare) < 0.5)
    lines = re.findall(r"\[Forever\] Shield talent: %s spell (\d+) (on|off)" % name, realm.log())
    results.check("the server logs each shield talent change", ("16542", "on") in lines and ("16542", "off") in lines,
                  "%r" % lines)

# Master of Defense: a level 60 boar attacks the Warrior.
here = pos(c, me)
c.gm(".npc spawn add %d" % BOAR, wait=1.5)
low = int(realm.sql("mangos", "SELECT MAX(guid) FROM creature WHERE id = %d" % BOAR).split()[-1])
boar = (0xF130 << 48) | (BOAR << 24) | low
c.attacker = boar
c.gm(".npc set level %d" % LEVEL, select=boar)
c.gm(".modify hp 1000000 1000000", select=boar)
c.move(MSG_MOVE_HEARTBEAT, here[0] - 2.0, here[1], here[2], 0.0)
c.pump(1)


def fight(seconds):
    """The boar swings at the Warrior. (dodges and parries, 23602 Rage gains, Master of Defense lines)."""
    c.gm(".aura %d" % EVASION, select=me)
    realm.mark("fight")
    start_avoided, start_energize = len(c.avoided), len(c.energize)
    c.select(boar)
    c.send(CMSG_ATTACKSWING, struct.pack("<Q", boar))
    c.pump(seconds)
    avoided = [s for s in c.avoided[start_avoided:] if s in (VICTIMSTATE_DODGE, VICTIMSTATE_PARRY)]
    rage = [amount for spell, amount in c.energize[start_energize:] if spell == RAGE_SPELL]
    lines = re.findall(r"\[Forever\] Master of Defense: %s (dodge|parry)" % name, realm.log())
    return avoided, rage, lines


if forever:
    equip(shield)
    avoided, rage, lines = fight(SWING_SECONDS)
    results.check("with the shield the Warrior dodges or parries the boar", len(avoided) >= 1, "%d" % len(avoided))
    results.check("with the shield each dodge and parry triggers Master of Defense 2/2 (100%)",
                  len(lines) == len(avoided), "%d lines, %d dodges/parries" % (len(lines), len(avoided)))
    results.check("each of them gives 5 Rage (23602); blocks may add Shield Specialization's",
                  len(rage) >= len(avoided) and set(rage) <= {50}, "rage %r, avoided %d" % (rage, len(avoided)))
    unequip_off_hand()
    avoided, rage, lines = fight(SWING_SECONDS)
    results.check("without the shield the Warrior still dodges or parries", len(avoided) >= 1, "%d" % len(avoided))
    results.check("without the shield no Master of Defense Rage", not lines and not rage,
                  "lines %r, rage %r" % (lines, rage))
else:
    avoided, rage, lines = fight(SWING_SECONDS)
    results.check("vanilla: no Master of Defense line", not lines, "%r" % lines)
c.send(CMSG_ATTACKSTOP)
c.pump(1)
c.gm(".unaura %d" % EVASION, select=me)


def duration_of(spell):
    """The boar casts `spell` at the Warrior until it lands (a miss or resist tries again). The new aura
    durations that the client gets."""
    for _ in range(5):
        start = len(c.durations)
        c.gm(".cast back %d triggered" % spell, select=boar, wait=1.5)
        got = c.durations[start:]
        if got:
            return got
        c.pump(4)
    return []


realm.mark("iron will")
for spell, stock in ((HAMMER_OF_JUSTICE_1, 3000), (FEAR_1, 10000)):
    expected = stock * 85 // 100 if forever else stock
    got = duration_of(spell)
    results.check("spell %d on the Warrior lasts %d ms (%s)" % (spell, expected, "Iron Will 5/5" if forever else "stock"),
                  any(abs(d - expected) <= 100 for d in got), "durations %r" % got)
    c.pump(stock / 1000.0 + 1)
iron_lines = re.findall(r"\[Forever\] Iron Will: %s spell (\d+) (\d+) -> (\d+) ms" % name, realm.log())
if forever:
    results.check("each shortened effect logs an Iron Will line",
                  {(s, a, b) for s, a, b in iron_lines} >= {("853", "3000", "2550"), ("5782", "10000", "8500")},
                  "%r" % iron_lines)
else:
    results.check("vanilla: no Iron Will line", not iron_lines, "%r" % iron_lines)

kill(c, boar)
c.gm(".npc spawn delete", select=boar, wait=1.0)
c.gm(".cheat god off", select=me)
results.finish()
