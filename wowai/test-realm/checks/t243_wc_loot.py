#!/usr/bin/env python3
"""In-game check of jvbastel/WOWAI#243, the Forever loot of Wailing Caverns (spec #240), on the check kit. Each step
is one run on the same slot; an acquire between them restarts the slot with the other rules and keeps the database
(same lease), as in t242_rfc_loot.py:

    wowai/test-realm/realm.sh --ticket N acquire --forever
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t243_wc_loot.py forever
    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t243_wc_loot.py vanilla

An Orc Rogue spawns Lady Anacondra (3671) where it stands, kills her with the GM `.die` (its own killing blow, so
it may loot) and opens her loot window. It asks the server for item and quest data as the client does for a tooltip
and the quest log (CMSG_ITEM_QUERY_SINGLE, CMSG_QUEST_QUERY).

forever  (rules on) forever_creature_loot_template holds each drop row of forever/spec/loot_wailing_caverns.tsv.
         Lady Anacondra, with the Forever row of Snake Eye Kaleidoscope (71101) forced to 100%, drops it and one
         item of her stock equal-chance group (5404, 6446, 10412), which has its Forever quality. Tail Spike (6448)
         has its Forever quality and damage. Quest 914 offers Hammerbone (71106) next to its two stock choices;
         quest 962 offers Heat Resistant Mitts (71104) and Safety Boots (71105).
vanilla  (rules off) Tail Spike has its stock quality and damage, Snake Eye Kaleidoscope is no item, quests 914 and
         962 offer their stock choices, and Lady Anacondra, with the forced row still in
         forever_creature_loot_template, drops no Kaleidoscope.
"""
import sys

from kit import Realm, Results, clear_backpack
from loot_kit import (CREATURE_LOOT, RARE, UNCOMMON, LootClient, forced_drop, kill_and_loot, spec_drops,
                      spec_drops_loaded)

ANACONDRA = 3671
KALEIDOSCOPE = 71101
# The stock equal-chance group of Lady Anacondra (reference 30126) with the Forever quality of each item.
ANACONDRA_DROPS = {5404: 3, 6446: 2, 10412: 3}
TAIL_SPIKE = 6448
LEADERS, LEADERS_STOCK, HAMMERBONE = 914, [6505, 6504], 71106
SERPENTBLOOM, MITTS, BOOTS = 962, 71104, 71105
# The spec drops (forever/spec/loot_wailing_caverns.tsv): (boss loot entry, item): chance.
DROPS = spec_drops("wailing_caverns")



mode = sys.argv[1] if len(sys.argv) > 1 else ""
if mode not in ("forever", "vanilla"):
    sys.exit("usage: t243_wc_loot.py forever|vanilla")

realm = Realm()
results = Results()
realm.mark("t243 %s" % mode)
c = realm.login("PLAYER243", "Wailingrog", race=2, cls=4, client_class=LootClient)

if mode == "forever":
    ok, loaded = spec_drops_loaded(realm, DROPS)
    results.check("%s holds each WC drop row of the spec, outside the stock groups, from patch 0" % CREATURE_LOOT,
                  ok, "got %r" % loaded)
    clear_backpack(realm, c)
    with forced_drop(realm, c, ANACONDRA, KALEIDOSCOPE):
        loot = kill_and_loot(realm, c, ANACONDRA)
    results.check("Lady Anacondra drops the new Snake Eye Kaleidoscope (%d)" % KALEIDOSCOPE,
                  loot and KALEIDOSCOPE in loot, "loot %r" % loot)
    stock = [item for item in (loot or []) if item in ANACONDRA_DROPS]
    results.check("Lady Anacondra drops one item of her stock equal-chance group", len(stock) == 1, "loot %r" % loot)
    answer = c.query_item(stock[0]) if stock else None
    results.check("that stock boss drop has its Forever quality",
                  answer not in (None, "missing") and answer[0] == ANACONDRA_DROPS[stock[0]], "answer %r" % (answer,))
    answer = c.query_item(KALEIDOSCOPE)
    results.check("Snake Eye Kaleidoscope is an item (rare)", answer not in (None, "missing") and answer[0] == RARE,
                  "answer %r" % (answer,))
    answer = c.query_item(TAIL_SPIKE)
    results.check("Tail Spike has its Forever quality and damage (rare, 17-33)", answer == (RARE, (17, 33)),
                  "answer %r" % (answer,))
    choices = c.quest_choices(LEADERS)
    results.check("quest %d offers Hammerbone (%d)" % (LEADERS, HAMMERBONE), choices == LEADERS_STOCK + [HAMMERBONE],
                  "choices %r" % choices)
    choices = c.quest_choices(SERPENTBLOOM)
    results.check("quest %d offers Heat Resistant Mitts and Safety Boots" % SERPENTBLOOM, choices == [MITTS, BOOTS],
                  "choices %r" % choices)
else:
    answer = c.query_item(TAIL_SPIKE)
    results.check("Tail Spike has its stock quality and damage (uncommon, 14-26)", answer == (UNCOMMON, (14, 26)),
                  "answer %r" % (answer,))
    answer = c.query_item(KALEIDOSCOPE)
    results.check("Snake Eye Kaleidoscope is not loaded", answer == "missing", "answer %r" % (answer,))
    choices = c.quest_choices(LEADERS)
    results.check("quest %d offers its two stock choices" % LEADERS, choices == LEADERS_STOCK, "choices %r" % choices)
    choices = c.quest_choices(SERPENTBLOOM)
    results.check("quest %d offers no choice (stock)" % SERPENTBLOOM, choices == [], "choices %r" % choices)
    with forced_drop(realm, c, ANACONDRA, KALEIDOSCOPE):
        loot = kill_and_loot(realm, c, ANACONDRA)
        results.check("Lady Anacondra drops no Kaleidoscope, even at 100%% in %s" % CREATURE_LOOT,
                      loot is not None and KALEIDOSCOPE not in loot, "loot %r" % loot)

results.finish()
