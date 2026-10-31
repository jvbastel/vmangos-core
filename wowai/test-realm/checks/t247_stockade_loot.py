#!/usr/bin/env python3
"""In-game check of jvbastel/WOWAI#247, the Forever loot of the Stockade (spec #240), on the check kit. Each step is
one run on the same slot; an acquire between them restarts the slot with the other rules and keeps the database
(same lease), as in t242_rfc_loot.py:

    wowai/test-realm/realm.sh --ticket N acquire --forever
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t247_stockade_loot.py forever
    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t247_stockade_loot.py vanilla

A Human Rogue spawns Kam Deepfury (1666) where it stands, kills him with the GM `.die` (the killing blow is its own,
so it may loot) and opens his loot window. It asks the server for item and quest data as the client does for a
tooltip and the quest log (CMSG_ITEM_QUERY_SINGLE, CMSG_QUEST_QUERY). The client class is a copy of the one in
t242_rfc_loot.py (a check script runs on its own and cannot import another check).

Forced drop: in forever_creature_loot_template, the Forever row of Demolition Girdle (71503) and Kam's copied row of
his stock reference 30098 (whose one equal-chance item is Kam's Walking Stick, 2280) are set to 100%, then restored.

forever  (rules on) forever_creature_loot_template holds each drop row of forever/spec/loot_stockade.tsv. Kam
         Deepfury drops his stock Kam's Walking Stick, which is rare with its Forever damage (39-60), and the new
         Demolition Girdle (rare). Quest 377 offers Town Clerk's Mittens (71516) next to its two stock choices.
vanilla  (rules off) Kam's Walking Stick has its stock quality and damage (uncommon, 41-62), Demolition Girdle is no
         item, and quest 377 offers its two stock choices. Kam, with the forced rows still in
         forever_creature_loot_template, drops no Demolition Girdle.
"""
import sys

from kit import Realm, Results, clear_backpack
from loot_kit import (CREATURE_LOOT, RARE, UNCOMMON, LootClient, forced_drop, kill_and_loot, spec_drops,
                      spec_drops_loaded)

TARGORR, KAM, HAMHOCK, DEXTREN, BAZIL = 1696, 1666, 1717, 1663, 1716
# The restore takes the spec chance and the stock table's chance, not values read at the start, so a run that died
# while forced does not leave 100% behind.
KAM_REFERENCE = 30098
WALKING_STICK, GIRDLE, MITTENS = 2280, 71503, 71516
CRIME, CRIME_STOCK = 377, [2033, 2906]
# The spec drops (forever/spec/loot_stockade.tsv): (boss loot entry, item): chance.
DROPS = spec_drops("stockade")


def reference_chance():
    """The chance of Kam's copied row of his stock reference in the Forever table (as text), or ""."""
    return realm.sql("mangos", "SELECT ChanceOrQuestChance FROM %s WHERE entry = %d AND mincountOrRef = -%d"
                     % (CREATURE_LOOT, KAM, KAM_REFERENCE)).strip()


mode = sys.argv[1] if len(sys.argv) > 1 else ""
if mode not in ("forever", "vanilla"):
    sys.exit("usage: t247_stockade_loot.py forever|vanilla")

realm = Realm()
results = Results()
realm.mark("t247 %s" % mode)
c = realm.login("PLAYER247", "Stockaderog", race=1, cls=4, client_class=LootClient)
reference = reference_chance()
results.check("%s holds Kam's copied row of his stock reference %d" % (CREATURE_LOOT, KAM_REFERENCE), reference != "",
              "no row")

if mode == "forever":
    ok, loaded = spec_drops_loaded(realm, DROPS)
    results.check("%s holds each drop row of the spec, outside the stock groups, from patch 0" % CREATURE_LOOT,
                  ok, "got %r" % loaded)
    clear_backpack(realm, c)
    # Kam's Demolition Girdle row and his reference row at 100%: each kill drops both items.
    with forced_drop(realm, c, KAM, GIRDLE, reference=KAM_REFERENCE):
        loot = kill_and_loot(realm, c, KAM)
    results.check("Kam Deepfury drops his stock Kam's Walking Stick (%d)" % WALKING_STICK, loot and WALKING_STICK in loot,
                  "loot %r" % loot)
    results.check("Kam Deepfury drops the new Demolition Girdle (%d)" % GIRDLE, loot and GIRDLE in loot, "loot %r" % loot)
    answer = c.query_item(WALKING_STICK)
    results.check("Kam's Walking Stick has its Forever quality and damage (rare, 39-60)", answer == (RARE, (39, 60)),
                  "answer %r" % (answer,))
    answer = c.query_item(GIRDLE)
    results.check("Demolition Girdle is an item (rare)", answer not in (None, "missing") and answer[0] == RARE,
                  "answer %r" % (answer,))
    choices = c.quest_choices(CRIME)
    results.check("quest %d offers Town Clerk's Mittens (%d)" % (CRIME, MITTENS), choices == CRIME_STOCK + [MITTENS],
                  "choices %r" % choices)
else:
    answer = c.query_item(WALKING_STICK)
    results.check("Kam's Walking Stick has its stock quality and damage (uncommon, 41-62)", answer == (UNCOMMON, (41, 62)),
                  "answer %r" % (answer,))
    answer = c.query_item(GIRDLE)
    results.check("Demolition Girdle is not loaded (the server does not know the item)", answer == "missing",
                  "answer %r" % (answer,))
    choices = c.quest_choices(CRIME)
    results.check("quest %d offers its two stock choices" % CRIME, choices == CRIME_STOCK, "choices %r" % choices)
    # Kam's Demolition Girdle row and his reference row at 100%: each kill drops both items.
    with forced_drop(realm, c, KAM, GIRDLE, reference=KAM_REFERENCE):
        loot = kill_and_loot(realm, c, KAM)
    results.check("Kam drops no Demolition Girdle, even at 100%% in %s" % CREATURE_LOOT,
                  loot is not None and GIRDLE not in loot, "loot %r" % loot)

results.finish()
