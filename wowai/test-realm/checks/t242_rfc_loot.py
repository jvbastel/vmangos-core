#!/usr/bin/env python3
"""In-game check of jvbastel/WOWAI#242, the Forever loot of Ragefire Chasm (spec #240), on the check kit. Each step
is one run on the same slot; an acquire between them restarts the slot with the other rules and keeps the database
(same lease), as in t233_switch_cycle.py:

    wowai/test-realm/realm.sh --ticket N acquire --forever
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t242_rfc_loot.py forever
    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t242_rfc_loot.py vanilla
    wowai/test-realm/realm.sh --ticket N acquire --forever
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t242_rfc_loot.py forever-again

An Orc Rogue (not a Warrior) spawns Ragefire Chasm bosses where it stands, kills them with the GM `.die` (the
killing blow is its own, so it may loot) and opens their loot window. It asks the server for item and quest data
as the client does for a tooltip and the quest log (CMSG_ITEM_QUERY_SINGLE, CMSG_QUEST_QUERY).

forever        (rules on) forever_creature_loot_template holds each drop row of forever/spec/loot_ragefire_chasm.tsv.
               Oggleflint (11517), with the Forever row of Trogg Scepter (71000) forced to 100%, drops it.
               Taragaman (11520) drops one of its three stock items (its stock equal-chance group), and that item
               has its Forever quality (rare). Cursed Felblade (14145) has its Forever damage. Quest 5724 offers
               Garrison Cuffs (71006) next to its two stock choices. The rogue gets a Trogg Scepter to keep.
vanilla        (rules off) Cursed Felblade has its stock stats, Trogg Scepter is no item (the server answers "not
               found" and keeps the rogue's row without loading it), and quest 5724 offers its two stock choices.
               Oggleflint, with the forced row still in forever_creature_loot_template, drops no Trogg Scepter.
forever-again  (rules on) The Trogg Scepter is an item again and the rogue's row is still there.
"""
import sys

from kit import Realm, Results, clear_backpack
from loot_kit import (CREATURE_LOOT, RARE, UNCOMMON, LootClient, forced_drop, kill_and_loot, spec_drops,
                      spec_drops_loaded)

OGGLEFLINT, TARAGAMAN = 11517, 11520
TROGG_SCEPTER, GARRISON_CUFFS, CURSED_FELBLADE = 71000, 71006, 14145
TARAGAMAN_DROPS = (14145, 14148, 14149)
SATCHEL, SATCHEL_STOCK = 5724, [15452, 15453]
# The spec drops (forever/spec/loot_ragefire_chasm.tsv): (boss loot entry, item): chance.
DROPS = spec_drops("ragefire_chasm")
KEPT_ITEM = r"\[Forever\] Item %d exists only under the Forever rules" % TROGG_SCEPTER


def scepter_rows():
    c.gm(".save", select=c.guid)
    c.pump(1)
    return realm.sql("characters", "SELECT COUNT(*) FROM character_inventory WHERE guid = %d AND item_id = %d"
                     % (c.guid, TROGG_SCEPTER)).strip()


mode = sys.argv[1] if len(sys.argv) > 1 else ""
if mode not in ("forever", "vanilla", "forever-again"):
    sys.exit("usage: t242_rfc_loot.py forever|vanilla|forever-again")

realm = Realm()
results = Results()
realm.mark("t242 %s" % mode)
c = realm.login("PLAYER242", "Ragefirerog", race=2, cls=4, client_class=LootClient)

if mode == "forever":
    ok, loaded = spec_drops_loaded(realm, DROPS)
    results.check("%s holds each drop row of the spec, outside the stock groups, from patch 0" % CREATURE_LOOT,
                  ok, "got %r" % loaded)
    clear_backpack(realm, c)
    # The spec chance is 3.22%: at 100% Oggleflint drops it on each kill.
    with forced_drop(realm, c, OGGLEFLINT, TROGG_SCEPTER):
        loot = kill_and_loot(realm, c, OGGLEFLINT)
    results.check("Oggleflint drops the new Trogg Scepter (%d)" % TROGG_SCEPTER, loot and TROGG_SCEPTER in loot,
                  "loot %r" % loot)
    loot = kill_and_loot(realm, c, TARAGAMAN)
    stock = [item for item in (loot or []) if item in TARAGAMAN_DROPS]
    results.check("Taragaman drops one item of its stock equal-chance group", len(stock) == 1, "loot %r" % loot)
    answer = c.query_item(stock[0]) if stock else None
    results.check("that stock boss drop is rare (Forever)", answer not in (None, "missing") and answer[0] == RARE,
                  "answer %r" % (answer,))
    answer = c.query_item(CURSED_FELBLADE)
    results.check("Cursed Felblade has its Forever quality and damage (rare, 19-37)", answer == (RARE, (19, 37)),
                  "answer %r" % (answer,))
    choices = c.quest_choices(SATCHEL)
    results.check("quest %d offers Garrison Cuffs (%d)" % (SATCHEL, GARRISON_CUFFS),
                  choices == SATCHEL_STOCK + [GARRISON_CUFFS], "choices %r" % choices)
    c.gm(".additem %d" % TROGG_SCEPTER, select=c.guid)
    results.check("the rogue keeps a Trogg Scepter", scepter_rows() == "1", "rows %s" % scepter_rows())
elif mode == "vanilla":
    answer = c.query_item(CURSED_FELBLADE)
    results.check("Cursed Felblade has its stock quality and damage (uncommon, 16-31)", answer == (UNCOMMON, (16, 31)),
                  "answer %r" % (answer,))
    answer = c.query_item(TROGG_SCEPTER)
    results.check("Trogg Scepter is not loaded (the server does not know the item)", answer == "missing",
                  "answer %r" % (answer,))
    results.check("the server keeps the rogue's Trogg Scepter without loading it", realm.wait_log(KEPT_ITEM, 5, c))
    results.check("the rogue's Trogg Scepter row stays through a save", scepter_rows() == "1", "rows %s" % scepter_rows())
    choices = c.quest_choices(SATCHEL)
    results.check("quest %d offers its two stock choices" % SATCHEL, choices == SATCHEL_STOCK, "choices %r" % choices)
    with forced_drop(realm, c, OGGLEFLINT, TROGG_SCEPTER):
        loot = kill_and_loot(realm, c, OGGLEFLINT)
    results.check("Oggleflint drops no Trogg Scepter, even at 100%% in %s" % CREATURE_LOOT,
                  loot is not None and TROGG_SCEPTER not in loot, "loot %r" % loot)
else:
    answer = c.query_item(TROGG_SCEPTER)
    results.check("Trogg Scepter is an item again (rare)", answer not in (None, "missing") and answer[0] == RARE,
                  "answer %r" % (answer,))
    results.check("no Forever item is kept unloaded", not realm.wait_log(KEPT_ITEM, 2, c))
    results.check("the rogue's Trogg Scepter row is still there", scepter_rows() == "1", "rows %s" % scepter_rows())

results.finish()
