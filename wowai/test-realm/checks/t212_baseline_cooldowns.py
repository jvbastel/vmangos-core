#!/usr/bin/env python3
"""In-game check of jvbastel/WOWAI#212 (spec #204): the Forever cooldowns of Retaliation, Recklessness and
Shield Wall, and the Forever baseline abilities that a Warrior learns at their level with no trainer. Each
step is one run on the same slot; an acquire between them restarts the slot with the other rules and keeps
the database (same lease):

    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t212_baseline_cooldowns.py vanilla
    wowai/test-realm/realm.sh --ticket N acquire --forever
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t212_baseline_cooldowns.py forever

vanilla (rules off) A level 30 Warrior (Basewar) casts Retaliation: 30 min cooldown. Recklessness right
        after fails: the three share one 30 min category. The check clears the cooldowns and logs out; the
        realm stores the vanilla rules.
forever (rules on) Basewar's first login switches and learns the baseline spells of level 30: Tactical
        Mastery (60125, level 14), Victory Rush (60123, 20), Slam rank 1 (60126, 20) and Berserker Rage
        (18499, 30), with no trainer. Retaliation has 15 min, Recklessness can be cast right after it and
        keeps 30 min, Shield Wall (with a shield) can be cast after both and has 15 min. A new Warrior
        (Basenew, level 1) learns each baseline spell at the level-up to its level, not before.
"""
import struct
import sys

from kit import Realm, Results, clear_backpack
from wowclient import SpellClient

CMSG_AUTOEQUIP_ITEM = 266

ACCOUNT, NAME = "PLAYER212", "Basewar"
NEW_ACCOUNT, NEW_NAME = "PLAYER212B", "Basenew"
LEVEL = 30
BATTLE_STANCE, DEFENSIVE_STANCE, BERSERKER_STANCE = 2457, 71, 2458
RETALIATION, RECKLESSNESS, SHIELD_WALL = 20230, 1719, 871
LARGE_ROUND_SHIELD = 2129
WORN_SHORTSWORD = 25
MAIN_HAND_SLOT, OFF_HAND_SLOT = 15, 16
# The baseline spells and their levels (forever/warrior.tsv).
BASELINE = {60125: 14, 60123: 20, 60126: 20, 18499: 30}
MINUTE = 60
SWITCH = r"\[Forever\] %s switches from the vanilla rules to the Forever rules"
LEARNS = r"\[Forever\] %s learns %d baseline spells of level %d\."


mode = sys.argv[1] if len(sys.argv) > 1 else ""
if mode not in ("vanilla", "forever"):
    sys.exit("usage: t212_baseline_cooldowns.py vanilla|forever")

realm = Realm()
results = Results()
realm.mark("t212 " + mode)
c = realm.login(ACCOUNT, NAME, race=2, cls=1, client_class=SpellClient)
me = c.guid
c.pump(2)
c.gm(".gm off")


def cooldown_left(spell):
    """Seconds left of the saved cooldown of a spell (its own or its category's), or None."""
    c.gm(".save", select=me)
    c.pump(1)
    row = realm.sql("characters", "SELECT spell_expire_time, category_expire_time, UNIX_TIMESTAMP() "
                                  "FROM character_spell_cooldown WHERE guid = %d AND spell = %d" % (me, spell))
    if not row:
        return None
    spell_end, category_end, now = (int(v) for v in row.split("\t"))
    left = [end - now for end in (spell_end, category_end) if 0 < end - now < 4 * 3600]
    return max(left) if left else None


def equip(entry):
    """Adds the item and equips it from the backpack, as the client does on a right click; True when worn."""
    c.gm(".additem %d" % entry, select=me)
    c.gm(".save", select=me)
    c.pump(1)
    slot = realm.sql("characters", "SELECT slot FROM character_inventory WHERE guid = %d AND bag = 0 AND slot >= 23 "
                                   "AND item_id = %d LIMIT 1" % (me, entry))
    if slot:
        c.send(CMSG_AUTOEQUIP_ITEM, struct.pack("<BB", 255, int(slot.split()[0])))
        c.pump(2)
    c.gm(".save", select=me)
    c.pump(1)
    worn = realm.sql("characters", "SELECT item_id FROM character_inventory WHERE guid = %d AND bag = 0 AND slot IN (%d, %d)"
                     % (me, MAIN_HAND_SLOT, OFF_HAND_SLOT)).split()
    return str(entry) in worn


def near(seconds, minutes):
    return seconds is not None and abs(seconds - minutes * MINUTE) <= MINUTE


def prepare():
    """The level, the stances and the three abilities; no cooldown left."""
    level = int(realm.sql("characters", "SELECT level FROM characters WHERE guid = %d" % me) or 1)
    if level < LEVEL:
        c.gm(".levelup %d" % (LEVEL - level), select=me)
    for spell in (BATTLE_STANCE, DEFENSIVE_STANCE, BERSERKER_STANCE, RETALIATION, RECKLESSNESS, SHIELD_WALL):
        c.gm(".learn %d" % spell, select=me)
    c.gm(".cooldown clear", select=me)
    c.pump(1)


if mode == "vanilla":
    prepare()
    results.check("no Forever baseline spell under the vanilla rules",
                  not {60123, 60125, 60126} & set(c.learned), "%r" % c.learned)
    c.cast_went(BATTLE_STANCE)
    results.check("Retaliation is cast", c.cast_went(RETALIATION))
    left = cooldown_left(RETALIATION)
    results.check("Retaliation has the stock 30 min cooldown", near(left, 30), "%r s" % left)
    c.cast_went(BERSERKER_STANCE)
    results.check("Recklessness shares the cooldown of Retaliation (stock category)", not c.cast_went(RECKLESSNESS))
    c.gm(".cooldown clear", select=me)
    c.gm(".save", select=me)
    c.pump(1)
else:
    results.check("the first Forever login of the vanilla Warrior switches", realm.wait_log(SWITCH % NAME, 5, c))
    results.check("the switch teaches the 4 baseline spells of level 30",
                  realm.wait_log(LEARNS % (NAME, len(BASELINE), LEVEL), 3, c))
    spells = c.initial_spells or set()
    results.check("the login sends every baseline spell", set(BASELINE) <= spells,
                  "missing %r" % sorted(set(BASELINE) - spells))
    prepare()
    c.cast_went(BATTLE_STANCE)
    results.check("Retaliation is cast", c.cast_went(RETALIATION))
    left = cooldown_left(RETALIATION)
    results.check("Retaliation has the Forever 15 min cooldown", near(left, 15), "%r s" % left)
    c.cast_went(BERSERKER_STANCE)
    results.check("Recklessness can be cast right after Retaliation (no shared category)", c.cast_went(RECKLESSNESS))
    left = cooldown_left(RECKLESSNESS)
    results.check("Recklessness keeps its 30 min cooldown (build data)", near(left, 30), "%r s" % left)

    # The starting two-hand axe blocks the shield: a one-hand sword first, then the shield.
    clear_backpack(realm, c)
    worn = [equip(item) for item in (WORN_SHORTSWORD, LARGE_ROUND_SHIELD)]
    c.cast_went(DEFENSIVE_STANCE)
    results.check("Shield Wall can be cast after Retaliation and Recklessness", c.cast_went(SHIELD_WALL),
                  "sword and shield worn %r" % worn)
    left = cooldown_left(SHIELD_WALL)
    results.check("Shield Wall has the Forever 15 min cooldown", near(left, 15), "%r s" % left)
    c.gm(".cooldown clear", select=me)

    # A new Warrior learns each baseline spell at its level, with no trainer.
    n = realm.login(NEW_ACCOUNT, NEW_NAME, race=2, cls=1, client_class=SpellClient)
    n.pump(2)
    n.gm(".gm off")
    level = int(realm.sql("characters", "SELECT level FROM characters WHERE guid = %d" % n.guid) or 1)
    if level != 1:
        # A run before: back to level 1 without the baseline spells.
        n.gm(".levelup %d" % (1 - level), select=n.guid)
        for spell in BASELINE:
            n.gm(".unlearn %d" % spell, select=n.guid)
        n.pump(1)
    n.learned.clear()
    previous = 1
    for target in sorted(set(BASELINE.values())):
        n.gm(".levelup %d" % (target - 1 - previous), select=n.guid)
        n.pump(1)
        early = [spell for spell in n.learned if spell in BASELINE]
        results.check("no baseline spell of level %d at level %d" % (target, target - 1),
                      all(BASELINE[spell] < target for spell in early), "learned %r" % early)
        start = len(n.learned)
        n.gm(".levelup 1", select=n.guid)
        n.pump(2)
        expected = {spell for spell, lvl in BASELINE.items() if lvl == target}
        got = set(n.learned[start:]) & set(BASELINE)
        results.check("level %d teaches %r" % (target, sorted(expected)), got == expected, "learned %r" % sorted(got))
        previous = target

results.finish()
