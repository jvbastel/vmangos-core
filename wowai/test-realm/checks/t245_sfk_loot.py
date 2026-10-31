#!/usr/bin/env python3
"""In-game check of jvbastel/WOWAI#245, the Forever loot of Shadowfang Keep (spec #240), on the check kit. Each step
is one run on the same slot; an acquire between them restarts the slot with the other rules and keeps the database
(same lease), as in t242_rfc_loot.py:

    wowai/test-realm/realm.sh --ticket N acquire --forever
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t245_sfk_loot.py forever
    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t245_sfk_loot.py vanilla

An Undead Rogue spawns Rethilgore where it stands, kills him with the GM `.die` (the killing blow is its own, so it
may loot) and opens his loot window. It asks the server for item and quest data as the client does for a tooltip
and the quest log (CMSG_ITEM_QUERY_SINGLE, CMSG_QUEST_QUERY). The loot client is the one of t242_rfc_loot.py, with
the fixed rewards of the quest answer too.

forever  (rules on) forever_creature_loot_template holds each drop row of forever/spec/loot_shadowfang_keep.tsv.
         Rethilgore (3914), with the Forever row of Cell Keeper's Claws (71300) forced to 100%, drops it and his
         stock Rugged Spaulders (5254, a stock row at 100%), which is rare in Forever (common in stock). Butcher's
         Cleaver (1292) has its Forever damage. Quest 1098 offers Ghostly Mantle and the two new shoulders as choices
         and has no fixed reward; quest 1013 offers Tattered Mittens (71309) next to its two stock choices.
vanilla  (rules off) Rugged Spaulders is common and Butcher's Cleaver has its stock damage, Cell Keeper's Claws is
         no item, quest 1098 gives Ghostly Mantle as its fixed reward with no choices, and Rethilgore, with the forced
         row still in forever_creature_loot_template, drops no Cell Keeper's Claws.
"""
import sys

from kit import Realm, Results, clear_backpack
from loot_kit import (CREATURE_LOOT, RARE, LootClient, forced_drop, kill_and_loot, spec_drops,
                      spec_drops_loaded)

RETHILGORE = 3914
CLAWS, RUGGED_SPAULDERS, BUTCHERS_CLEAVER = 71300, 5254, 1292
DEATHSTALKERS, GHOSTLY_MANTLE, DEATHSTALKERS_NEW = 1098, 3324, [71307, 71308]
BOOK_OF_UR, BOOK_OF_UR_STOCK, MITTENS = 1013, [6335, 4534], 71309
COMMON = 1
# The spec drops (forever/spec/loot_shadowfang_keep.tsv): (boss loot entry, item): chance.
DROPS = spec_drops("shadowfang_keep")


def forced_rethilgore_loot():
    """Rethilgore's loot with the Forever row of Cell Keeper's Claws at 100% (the spec chance is 1.68%)."""
    with forced_drop(realm, c, RETHILGORE, CLAWS):
        return kill_and_loot(realm, c, RETHILGORE)


mode = sys.argv[1] if len(sys.argv) > 1 else ""
if mode not in ("forever", "vanilla"):
    sys.exit("usage: t245_sfk_loot.py forever|vanilla")

realm = Realm()
results = Results()
realm.mark("t245 %s" % mode)
c = realm.login("PLAYER245", "Sfkrogue", race=5, cls=4, client_class=LootClient)

if mode == "forever":
    ok, loaded = spec_drops_loaded(realm, DROPS)
    results.check("%s holds each SFK drop row of the spec, outside the stock groups, from patch 0" % CREATURE_LOOT,
                  ok, "got %r" % loaded)
    clear_backpack(realm, c)
    loot = forced_rethilgore_loot()
    results.check("Rethilgore drops the new Cell Keeper's Claws (%d)" % CLAWS, loot and CLAWS in loot, "loot %r" % loot)
    results.check("Rethilgore drops his stock Rugged Spaulders (%d)" % RUGGED_SPAULDERS, loot and RUGGED_SPAULDERS in loot,
                  "loot %r" % loot)
    answer = c.query_item(RUGGED_SPAULDERS)
    results.check("Rugged Spaulders is rare (Forever)", answer not in (None, "missing") and answer[0] == RARE,
                  "answer %r" % (answer,))
    answer = c.query_item(CLAWS)
    results.check("Cell Keeper's Claws is a rare item with 25-48 damage", answer == (RARE, (25, 48)), "answer %r" % (answer,))
    answer = c.query_item(BUTCHERS_CLEAVER)
    results.check("Butcher's Cleaver has its Forever damage (rare, 19-36)", answer == (RARE, (19, 36)), "answer %r" % (answer,))
    rewards = c.quest_rewards(DEATHSTALKERS)
    results.check("quest %d offers Ghostly Mantle and the two new shoulders, with no fixed reward" % DEATHSTALKERS,
                  rewards == ([], [GHOSTLY_MANTLE] + DEATHSTALKERS_NEW), "rewards %r" % (rewards,))
    rewards = c.quest_rewards(BOOK_OF_UR)
    results.check("quest %d offers Tattered Mittens (%d)" % (BOOK_OF_UR, MITTENS),
                  rewards == ([], BOOK_OF_UR_STOCK + [MITTENS]), "rewards %r" % (rewards,))
else:
    answer = c.query_item(RUGGED_SPAULDERS)
    results.check("Rugged Spaulders is common (stock)", answer not in (None, "missing") and answer[0] == COMMON,
                  "answer %r" % (answer,))
    answer = c.query_item(BUTCHERS_CLEAVER)
    results.check("Butcher's Cleaver has its stock damage (rare, 23-32)", answer == (RARE, (23, 32)), "answer %r" % (answer,))
    answer = c.query_item(CLAWS)
    results.check("Cell Keeper's Claws is not loaded (the server does not know the item)", answer == "missing",
                  "answer %r" % (answer,))
    rewards = c.quest_rewards(DEATHSTALKERS)
    results.check("quest %d gives Ghostly Mantle as its fixed reward, with no choices" % DEATHSTALKERS,
                  rewards == ([GHOSTLY_MANTLE], []), "rewards %r" % (rewards,))
    rewards = c.quest_rewards(BOOK_OF_UR)
    results.check("quest %d offers its two stock choices" % BOOK_OF_UR, rewards == ([], BOOK_OF_UR_STOCK),
                  "rewards %r" % (rewards,))
    clear_backpack(realm, c)
    loot = forced_rethilgore_loot()
    results.check("Rethilgore drops no Cell Keeper's Claws, even at 100%% in %s" % CREATURE_LOOT,
                  loot is not None and CLAWS not in loot, "loot %r" % loot)

results.finish()
