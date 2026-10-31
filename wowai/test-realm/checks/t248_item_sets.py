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
import sys

from kit import Realm, Results
from set_kit import Wearer, set_bonuses
from wowclient import SpellClient

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
    return set_bonuses("loot_item_sets.tsv", set_id)


mode = sys.argv[1] if len(sys.argv) > 1 else ""
if mode not in ("forever", "vanilla"):
    sys.exit("usage: t248_item_sets.py forever|vanilla")

realm = Realm()
results = Results()
name = "Setdefias" if mode == "forever" else "Setdefvan"
c = realm.login("PLAYER248", name, race=2, cls=1, client_class=SpellClient)
me = c.guid
wearer = Wearer(realm, c, results)
auras, equipped, backpack_slot_of = wearer.auras, wearer.equipped, wearer.backpack_slot_of
wear, undress, check_bonuses = wearer.wear, wearer.undress, wearer.check_bonuses
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
    results.check("forever: three Embrace of the Viper pieces are worn", set(VIPER_PIECES[:3]) <= equipped())
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
