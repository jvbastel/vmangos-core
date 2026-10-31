#!/usr/bin/env python3
"""In-game check of jvbastel/WOWAI#206, the tracer bullet of the Forever rules (spec #204), on the check kit.

    # A slot with the Forever rules:
    wowai/test-realm/realm.sh --ticket N acquire --forever
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t206_forever_talent.py forever
    # The same slot without them:
    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t206_forever_talent.py vanilla

A player Warrior at level 20 learns talents with CMSG_LEARN_TALENT, as the talent window does. The talent
IDs and spells come from forever/spec/warrior.tsv (the full spec of #210) and from the stock Talent.dbc. Fury
tier 2 column 2 is a new Forever talent, Lingering Rage (talent 60002, rank 1 spell 60106), in the place
of the stock Improved Demoralizing Shout (talent 161, rank 1 spell 12324). Cruelty (talent 157, ranks
12320-12856 in both rule sets) gives the 5 Fury points that tier 2 needs.

forever: Lingering Rage teaches 60106, and the server refuses Improved Demoralizing Shout, which is not in
         the Forever trees. After a logout and a new login, the character still has 60106: in
         character_spell and in the SMSG_INITIAL_SPELLS of the login.
vanilla: the vanilla Warrior trees: the server refuses talent 60002, Improved Demoralizing Shout teaches
         12324, and no Forever spell is learned.
"""
import sys

from kit import Realm, Results, saved_spells
from wowclient import SpellClient

CRUELTY = 157
CRUELTY_SPELLS = (12320, 12852, 12853, 12855, 12856)
LINGERING_RAGE = 60002
FOREVER_LINGERING_RAGE_1 = 60106
IMPROVED_DEMORALIZING_SHOUT = 161
STOCK_IMPROVED_DEMORALIZING_SHOUT_1 = 12324
FOREVER_SPELLS = (60106, 60107, 60108, 60109, 60110)
LEVEL = 20


mode = sys.argv[1] if len(sys.argv) > 1 else ""
if mode not in ("forever", "vanilla"):
    sys.exit("usage: t206_forever_talent.py forever|vanilla")

realm = Realm()
results = Results()
name = "Foreverwar" if mode == "forever" else "Vanillawar"
c = realm.login("PLAYER206", name, race=2, cls=1, client_class=SpellClient)
me = c.guid
level = int(realm.sql("characters", "SELECT level FROM characters WHERE guid = %d" % me) or 1)
if level < LEVEL:
    c.gm(".levelup %d" % (LEVEL - level), select=me)
c.gm(".reset talents", select=me)
c.pump(1)


cruelty = c.learn_ranks(CRUELTY, 5)
results.check("Cruelty 5/5 is learned (the 5 Fury points of tier 2)", cruelty == list(CRUELTY_SPELLS),
              "learned %r" % cruelty)
lingering = c.learn_talent(LINGERING_RAGE)
demoralizing = c.learn_talent(IMPROVED_DEMORALIZING_SHOUT)

if mode == "forever":
    results.check("Lingering Rage teaches its Forever rank 1", lingering == [FOREVER_LINGERING_RAGE_1],
                  "learned %r" % lingering)
    results.check("Improved Demoralizing Shout is not in the Forever Warrior trees", demoralizing == [],
                  "learned %r" % demoralizing)
    results.check("the Forever rank is saved", FOREVER_LINGERING_RAGE_1 in saved_spells(realm, c, enabled_only=False))
    c.relog()
    results.check("after a relog the login sends the Forever rank",
                  c.initial_spells is not None and FOREVER_LINGERING_RAGE_1 in c.initial_spells,
                  "no SMSG_INITIAL_SPELLS" if c.initial_spells is None else "")
    saved = saved_spells(realm, c, enabled_only=False)
    results.check("after a relog the character still has the Forever rank", FOREVER_LINGERING_RAGE_1 in saved)
    results.check("the character has no stock Improved Demoralizing Shout", STOCK_IMPROVED_DEMORALIZING_SHOUT_1 not in saved)
else:
    results.check("the vanilla trees have no talent 60002", lingering == [], "learned %r" % lingering)
    results.check("Improved Demoralizing Shout is in the vanilla Fury tree",
                  demoralizing == [STOCK_IMPROVED_DEMORALIZING_SHOUT_1], "learned %r" % demoralizing)
    saved = saved_spells(realm, c, enabled_only=False)
    results.check("the stock rank is saved", STOCK_IMPROVED_DEMORALIZING_SHOUT_1 in saved)
    results.check("no Forever spell is learned", not saved & set(FOREVER_SPELLS), "%r" % (saved & set(FOREVER_SPELLS)))

results.finish()
