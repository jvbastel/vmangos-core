#!/usr/bin/env python3
"""In-game check of jvbastel/WOWAI#248, the Forever item sets with pieces in the six dungeons (spec #240), on the
check kit.

    # A slot with the Forever rules:
    wowai/test-realm/realm.sh --ticket N acquire --forever
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t248_item_sets.py forever
    # A slot without them:
    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t248_item_sets.py vanilla

A level 25 Orc Warrior wears set pieces (GM `.additem`, then CMSG_AUTOEQUIP_ITEM, as a player equips from the
bags) and reads its auras with `.list auras`: a set bonus is the aura of its spell. The Forever bonuses are the
ported bonus rows of forever/spec/loot_item_sets.tsv (Forever client build 1.60.1.70685). Every piece is patch 0.

forever: two Defias Leather pieces (Deadmines) give the Forever 2-piece bonus (+5 Arcane Resistance, 14707) and not
         the stock one (Armor 10, 9760); all five give every ported bonus (Daggers +1 at 5). Two Embrace of the Viper
         pieces (Wailing Caverns) give +10 Intellect (9106) and not the stock Nature damage 7 (7694); three give
         Attack Power 10 (9140) too, and not the stock Staves +2 (20607).
vanilla: two Defias Leather pieces give the stock 2-piece bonus (9760) and not 14707; two Embrace of the Viper pieces
         give the stock 7694 and not 9106.
"""
import re
import struct
import sys
from pathlib import Path

from kit import Realm, Results, clear_backpack
from wowclient import CMSG_DESTROYITEM, SpellClient

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "forever" / "generator"))
from forever_gen import item_sets, spec  # noqa: E402  (the reader of forever/spec/loot_item_sets.tsv)

CMSG_AUTOEQUIP_ITEM = 266
BACKPACK, EQUIPMENT_SLOTS = 255, 19
LEVEL = 25
DEFIAS, VIPER = 161, 162
# Armor, Belt, Boots, Gloves, Leggings.
DEFIAS_PIECES = (10399, 10403, 10402, 10401, 10400)
# Belt, Footpads, Leggings, Armor (the Wailing Caverns item rows), Gloves.
VIPER_PIECES = (10412, 10411, 10410, 6473, 10413)
# The stock 1.12 bonuses (ItemSet.dbc).
STOCK_DEFIAS = {2: [9760], 3: [14707], 4: [7534], 5: [9140]}
STOCK_VIPER = {2: [7694], 3: [20607], 4: [7679], 5: [9106]}


def forever_bonuses(set_id):
    """{threshold: [spell]} of the ported bonus rows of the set in forever/spec/loot_item_sets.tsv."""
    path = ROOT / "forever" / "spec" / "loot_item_sets.tsv"
    rows = item_sets.read_loot_rows(str(path), spec.data_lines(path.read_text(encoding="utf-8")))["item_sets"]
    found = {}
    for spell, threshold in item_sets.spells(next(s for s in rows if s.set_id == set_id)):
        found.setdefault(threshold, []).append(spell)
    return found


def auras():
    """The spell IDs of the auras on the character (`.list auras`)."""
    found = set()
    for line in c.gm(".list auras", select=me, wait=1.0):
        m = re.search(r"id: (\d+) eff: \d+ type: \d+", line)
        if m:
            found.add(int(m.group(1)))
    return found


def equipped():
    c.gm(".save", select=me)
    rows = realm.sql("characters", "SELECT item_id FROM character_inventory WHERE guid = %d AND bag = 0 AND slot < %d"
                     % (me, EQUIPMENT_SLOTS))
    return {int(row) for row in rows.split() if row.isdigit()}


def backpack_slot_of(entry):
    c.gm(".save", select=me)
    row = realm.sql("characters", "SELECT slot FROM character_inventory WHERE guid = %d AND bag = 0 AND slot >= 23 "
                    "AND item_id = %d ORDER BY slot LIMIT 1" % (me, entry))
    return int(row.split()[0]) if row else None


def wear(*entries):
    """Adds each item and equips it from the backpack. True when the server made each item."""
    made = True
    for entry in entries:
        c.gm(".additem %d" % entry, select=me)
        slot = backpack_slot_of(entry)
        if slot is None:
            made = False
            continue
        c.send(CMSG_AUTOEQUIP_ITEM, struct.pack("<BB", BACKPACK, slot))
        c.pump(1.5)
    return made


def undress():
    """Destroys every equipped item, so that the next set starts from no piece (and the check can run again)."""
    for slot in range(EQUIPMENT_SLOTS):
        c.send(CMSG_DESTROYITEM, struct.pack("<BBBBBB", BACKPACK, slot, 0, 0, 0, 0))
    c.pump(1.5)
    clear_backpack(realm, c)


def check_bonuses(label, bonuses, worn):
    """Each bonus up to `worn` pieces is on the character, each one above is not."""
    have = auras()
    for threshold, spells in sorted(bonuses.items()):
        on = threshold <= worn
        for spell in spells:
            results.check("%s, %d pieces: the %d-piece bonus %d is %s" % (label, worn, threshold, spell,
                                                                         "on" if on else "off"),
                          (spell in have) == on, "auras %s" % sorted(have))


mode = sys.argv[1] if len(sys.argv) > 1 else ""
if mode not in ("forever", "vanilla"):
    sys.exit("usage: t248_item_sets.py forever|vanilla")

realm = Realm()
results = Results()
name = "Setdefias" if mode == "forever" else "Setdefvan"
c = realm.login("PLAYER248", name, race=2, cls=1, client_class=SpellClient)
me = c.guid
level = int(realm.sql("characters", "SELECT level FROM characters WHERE guid = %d" % me) or 1)
if level < LEVEL:
    c.gm(".levelup %d" % (LEVEL - level), select=me)
undress()

if mode == "forever":
    defias, viper = forever_bonuses(DEFIAS), forever_bonuses(VIPER)
    results.check("forever: the spec ports Defias 2 = 14707 and Viper 2 = 9106", defias.get(2) == [14707]
                  and viper.get(2) == [9106], "%s %s" % (defias, viper))
    wear(*DEFIAS_PIECES[:2])
    results.check("forever: two Defias Leather pieces are worn", set(DEFIAS_PIECES[:2]) <= equipped())
    check_bonuses("forever Defias Leather", defias, 2)
    results.check("forever: the stock Defias 2-piece bonus (Armor 10, 9760) is gone", 9760 not in auras())
    wear(*DEFIAS_PIECES[2:])
    results.check("forever: five Defias Leather pieces are worn", set(DEFIAS_PIECES) <= equipped())
    check_bonuses("forever Defias Leather", defias, 5)
    undress()
    wear(*VIPER_PIECES[:2])
    results.check("forever: two Embrace of the Viper pieces are worn", set(VIPER_PIECES[:2]) <= equipped())
    check_bonuses("forever Embrace of the Viper", viper, 2)
    results.check("forever: the stock Viper 2-piece bonus (Nature damage 7, 7694) is gone", 7694 not in auras())
    wear(VIPER_PIECES[2])
    check_bonuses("forever Embrace of the Viper", viper, 3)
    results.check("forever: the stock Viper 3-piece bonus (Staves +2, 20607) is gone", 20607 not in auras())
else:
    wear(*DEFIAS_PIECES[:2])
    results.check("vanilla: two Defias Leather pieces are worn", set(DEFIAS_PIECES[:2]) <= equipped())
    check_bonuses("vanilla Defias Leather", STOCK_DEFIAS, 2)
    undress()
    wear(*VIPER_PIECES[:2])
    results.check("vanilla: two Embrace of the Viper pieces are worn", set(VIPER_PIECES[:2]) <= equipped())
    check_bonuses("vanilla Embrace of the Viper", STOCK_VIPER, 2)
undress()

results.finish()
