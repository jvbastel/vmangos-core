#!/usr/bin/env python3
"""In-game check of jvbastel/WOWAI#244, the Forever loot of the Deadmines (spec #240), on the check kit. Each step is
one run on the same slot; an acquire between them restarts the slot with the other rules and keeps the database
(same lease), as in t242_rfc_loot.py:

    wowai/test-realm/realm.sh --ticket N acquire --forever
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t244_deadmines.py forever
    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t244_deadmines.py vanilla

A Human Rogue spawns Sneed (643) where it stands, kills him with the GM `.die` (the killing blow is its own, so it
may loot) and opens his loot window. It asks the server for item and quest data as the client does for a tooltip
and the quest log (CMSG_ITEM_QUERY_SINGLE, CMSG_QUEST_QUERY).

forever  (rules on) forever_creature_loot_template holds each drop row of forever/spec/loot_deadmines.tsv. Sneed,
         with the Forever row of Bandsaw Wristbands (71201) forced to 100%, drops it and one item of his stock
         equal-chance group (Taskmaster Axe 5194 or Gold-flecked Gloves 5195), which has its Forever values: Gold-
         flecked Gloves is rare (stock uncommon), Taskmaster Axe has 31-48 damage (stock 42-64). Quest 167 offers
         Miner's Revenge (1893), Miner's Workgloves (71208) and Miner's Workboots (71209) as choices, with no fixed
         reward; quest 2040 offers Bravo's Armbands (71210) and Dreamer's Leggings (71211) next to its stock choices.
vanilla  (rules off) Gold-flecked Gloves is uncommon, Taskmaster Axe has 42-64 damage, Bandsaw Wristbands is no item,
         and quest 167 gives Miner's Revenge as a fixed reward with no choice.
"""
import sys

from kit import Realm, Results, clear_backpack
from loot_kit import (CREATURE_LOOT, RARE, UNCOMMON, LootClient, forced_drop, kill_and_loot, spec_drops,
                      spec_drops_loaded)

SNEED = 643
BANDSAW_WRISTBANDS = 71201
TASKMASTER_AXE, GOLD_FLECKED_GLOVES = 5194, 5195
SNEED_DROPS = (TASKMASTER_AXE, GOLD_FLECKED_GLOVES)
OH_BROTHER, MINERS_REVENGE = 167, 1893
ASSAULT, ASSAULT_STOCK = 2040, [7606, 7607]
# The spec drops (forever/spec/loot_deadmines.tsv): (boss loot entry, item): chance.
DROPS = spec_drops("deadmines")


def check_stock_drops(gloves_quality, axe_damage):
    """Checks the two stock boss drops of Sneed: the quality of the gloves and the damage of the axe."""
    answer = c.query_item(GOLD_FLECKED_GLOVES)
    results.check("Gold-flecked Gloves has quality %d" % gloves_quality,
                  answer not in (None, "missing") and answer[0] == gloves_quality, "answer %r" % (answer,))
    answer = c.query_item(TASKMASTER_AXE)
    results.check("Taskmaster Axe has %d-%d damage" % axe_damage,
                  answer not in (None, "missing") and answer[1] == axe_damage, "answer %r" % (answer,))


mode = sys.argv[1] if len(sys.argv) > 1 else ""
if mode not in ("forever", "vanilla"):
    sys.exit("usage: t244_deadmines.py forever|vanilla")

realm = Realm()
results = Results()
realm.mark("t244 %s" % mode)
c = realm.login("PLAYER244", "Deadminerog", race=1, cls=4, client_class=LootClient)

if mode == "forever":
    ok, loaded = spec_drops_loaded(realm, DROPS)
    results.check("%s holds each Deadmines drop row of the spec, outside the stock groups, from patch 0" % CREATURE_LOOT,
                  ok, "got %r" % loaded)
    clear_backpack(realm, c)
    with forced_drop(realm, c, SNEED, BANDSAW_WRISTBANDS):
        loot = kill_and_loot(realm, c, SNEED)
    results.check("Sneed drops the new Bandsaw Wristbands (%d)" % BANDSAW_WRISTBANDS,
                  loot and BANDSAW_WRISTBANDS in loot, "loot %r" % loot)
    stock = [item for item in (loot or []) if item in SNEED_DROPS]
    results.check("Sneed drops one item of his stock equal-chance group", len(stock) == 1, "loot %r" % loot)
    # Both stock drops, whichever one fell.
    check_stock_drops(RARE, (31, 48))
    answer = c.query_item(BANDSAW_WRISTBANDS)
    results.check("Bandsaw Wristbands is rare", answer not in (None, "missing") and answer[0] == RARE,
                  "answer %r" % (answer,))
    rewards = c.quest_rewards(OH_BROTHER)
    results.check("quest %d offers 1893, 71208 and 71209 as choices with no fixed reward" % OH_BROTHER,
                  rewards == ([], [MINERS_REVENGE, 71208, 71209]), "rewards %r" % (rewards,))
    rewards = c.quest_rewards(ASSAULT)
    results.check("quest %d offers Bravo's Armbands and Dreamer's Leggings" % ASSAULT,
                  rewards == ([], ASSAULT_STOCK + [71210, 71211]), "rewards %r" % (rewards,))
else:
    check_stock_drops(UNCOMMON, (42, 64))
    answer = c.query_item(BANDSAW_WRISTBANDS)
    results.check("Bandsaw Wristbands is not loaded (the server does not know the item)", answer == "missing",
                  "answer %r" % (answer,))
    rewards = c.quest_rewards(OH_BROTHER)
    results.check("quest %d gives Miner's Revenge as its fixed reward, with no choice" % OH_BROTHER,
                  rewards == ([MINERS_REVENGE], []), "rewards %r" % (rewards,))

results.finish()
