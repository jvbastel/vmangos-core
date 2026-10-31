#!/usr/bin/env python3
"""In-game check of jvbastel/WOWAI#246, the Forever loot of Blackfathom Deeps (spec #240), on the check kit (batched
test of spec #240). Each step is one run on the same slot; an acquire between them restarts the slot with the other
rules and keeps the database (same lease):

    wowai/test-realm/realm.sh --ticket N acquire --forever
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t246_bfd_loot.py forever
    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t246_bfd_loot.py vanilla

A Tauren Warrior spawns Ghamoo-ra (4887) where it stands, kills it with the GM `.die` (the killing blow is its own, so
it may loot) and opens its loot window. It asks the server for item and quest data as the client does for a tooltip
and the quest log (CMSG_ITEM_QUERY_SINGLE, CMSG_QUEST_QUERY). The client class is copied from t242_rfc_loot.py.

forever  (rules on) forever_creature_loot_template holds each drop row of forever/spec/loot_blackfathom_deeps.tsv.
         Ghamoo-ra, with the Forever row of Spiked Shell Band (71400) forced to 100%, drops it, and drops one of its
         two stock items (reference 30156, one equal-chance group): Tortoise Armor (6907) or Ghamoo-ra's Bind (6908),
         both rare in Forever (Ghamoo-ra's Bind is uncommon in stock). Strike of the Hydra (6909) keeps its stock
         damage, Darkwater Talwar (11121) has its Forever damage 26-49. Quest 6561 Blackfathom Villainy (Horde)
         offers Dark Ritual Leggings (71408) and Cultist's Armguards (71409) next to its two stock choices.
vanilla  (rules off) Darkwater Talwar has its stock quality and damage (uncommon, 20-39), Spiked Shell Band is no item
         and quest 6561 offers its two stock choices. Ghamoo-ra, with the forced row still in
         forever_creature_loot_template, drops no Spiked Shell Band.
"""
import sys

from kit import Realm, Results, clear_backpack
from loot_kit import (CREATURE_LOOT, RARE, UNCOMMON, LootClient, forced_drop, kill_and_loot, spec_drops,
                      spec_drops_loaded)

GHAMOORA = 4887
SHELL_BAND = 71400
GHAMOORA_DROPS = (6907, 6908)
DARKWATER_TALWAR, STRIKE_OF_THE_HYDRA = 11121, 6909
VILLAINY, VILLAINY_STOCK, VILLAINY_NEW = 6561, [7001, 7002], [71408, 71409]
# The spec drops (forever/spec/loot_blackfathom_deeps.tsv): (boss loot entry, item): chance.
GELIHAST, LORGUS, KELRIS = 6243, 12902, 4832
DROPS = spec_drops("blackfathom_deeps")


mode = sys.argv[1] if len(sys.argv) > 1 else ""
if mode not in ("forever", "vanilla"):
    sys.exit("usage: t246_bfd_loot.py forever|vanilla")

realm = Realm()
results = Results()
realm.mark("t246 %s" % mode)
c = realm.login("PLAYER246", "Bfdwarrior", race=6, cls=1, client_class=LootClient)

if mode == "forever":
    ok, loaded = spec_drops_loaded(realm, DROPS)
    results.check("%s holds each BFD drop row of the spec, outside the stock groups, from patch 0" % CREATURE_LOOT,
                  ok, "got %r" % loaded)
    clear_backpack(realm, c)
    with forced_drop(realm, c, GHAMOORA, SHELL_BAND):
        loot = kill_and_loot(realm, c, GHAMOORA)
    results.check("Ghamoo-ra drops the new Spiked Shell Band (%d)" % SHELL_BAND, loot and SHELL_BAND in loot,
                  "loot %r" % loot)
    stock = [item for item in (loot or []) if item in GHAMOORA_DROPS]
    results.check("Ghamoo-ra drops one item of its stock equal-chance group", len(stock) == 1, "loot %r" % loot)
    answer = c.query_item(stock[0]) if stock else None
    results.check("that stock boss drop is rare (Forever)", answer not in (None, "missing") and answer[0] == RARE,
                  "answer %r" % (answer,))
    answer = c.query_item(SHELL_BAND)
    results.check("Spiked Shell Band is a rare item", answer not in (None, "missing") and answer[0] == RARE,
                  "answer %r" % (answer,))
    answer = c.query_item(DARKWATER_TALWAR)
    results.check("Darkwater Talwar has its Forever quality and damage (rare, 26-49)", answer == (RARE, (26, 49)),
                  "answer %r" % (answer,))
    answer = c.query_item(STRIKE_OF_THE_HYDRA)
    results.check("Strike of the Hydra keeps its damage (rare, 67-102)", answer == (RARE, (67, 102)),
                  "answer %r" % (answer,))
    choices = c.quest_choices(VILLAINY)
    results.check("quest %d offers Dark Ritual Leggings and Cultist's Armguards" % VILLAINY,
                  choices == VILLAINY_STOCK + VILLAINY_NEW, "choices %r" % choices)
else:
    answer = c.query_item(DARKWATER_TALWAR)
    results.check("Darkwater Talwar has its stock quality and damage (uncommon, 20-39)", answer == (UNCOMMON, (20, 39)),
                  "answer %r" % (answer,))
    answer = c.query_item(SHELL_BAND)
    results.check("Spiked Shell Band is not loaded (the server does not know the item)", answer == "missing",
                  "answer %r" % (answer,))
    choices = c.quest_choices(VILLAINY)
    results.check("quest %d offers its two stock choices" % VILLAINY, choices == VILLAINY_STOCK, "choices %r" % choices)
    with forced_drop(realm, c, GHAMOORA, SHELL_BAND):
        loot = kill_and_loot(realm, c, GHAMOORA)
    results.check("Ghamoo-ra drops no Spiked Shell Band, even at 100%% in %s" % CREATURE_LOOT,
                  loot is not None and SHELL_BAND not in loot, "loot %r" % loot)

results.finish()
