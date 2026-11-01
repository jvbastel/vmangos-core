#!/usr/bin/env python3
"""In-game check of jvbastel/WOWAI#239, the trainers of the new Blacksmithing recipes (spec #225), on the check kit.

    # A slot with the Forever rules:
    wowai/test-realm/realm.sh --ticket N acquire --forever
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t239_bs_trainers.py forever
    # A slot without them:
    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t239_bs_trainers.py vanilla

An Orc Hunter loses Blacksmithing (so that a run again starts the same), learns Apprentice Blacksmithing
(2018), and opens the trainer windows of two Blacksmithing trainers that a GM spawns at its feet: Dwukk (3174,
an Apprentice trainer: stock teaches up to skill 70) and Snarl (1383, a Journeyman trainer). The values come
from forever/spec/blacksmithing_trainers.tsv and blacksmithing.tsv:
- the 3 copper boots (learn spells 61197, 61196, 61198) at skill 35, from the trainers of Copper Chain Belt;
- the 5 belts (61257-61261) at skill 150, from the trainers of Iron Buckle (Journeyman and higher);
- Copper Rod (61001) with Apprentice Blacksmithing, as Rough Copper Vest (12260).

forever: the new Apprentice knows Copper Rod with no trainer step. Dwukk offers the 3 boots at Blacksmithing
         35 (red at skill 34, green at 35) and no belt; buying Strange Copper Boots teaches recipe 61000. Snarl
         offers the 5 belts at Blacksmithing 150 (red at 149, green at 150); buying Officer's Belt teaches 61078.
vanilla: the new Apprentice knows Rough Copper Vest and not Copper Rod, and no trainer offers a new learn spell.
"""
import sys

from kit import Realm, Results, clear_backpack, saved_spells, spawn
from wowclient import TrainerClient

BLACKSMITHING_SKILL, BLACKSMITHING_APPRENTICE = 164, 2018
ROUGH_COPPER_VEST, COPPER_ROD = 12260, 61001
APPRENTICE_TRAINER, JOURNEYMAN_TRAINER = 3174, 1383
# learn spell -> recipe.
BOOTS = {61197: 61000, 61196: 61003, 61198: 61004}
BELTS = {61257: 61078, 61258: 61079, 61259: 61080, 61260: 61081, 61261: 61082}
BOOTS_SKILL, BELTS_SKILL = 35, 150
STRANGE_COPPER_BOOTS_LEARN, OFFICERS_BELT_LEARN = 61197, 61257
# SMSG_TRAINER_LIST spell states (wowclient.TrainerClient): 0 green (can learn), 1 red (cannot yet).
GREEN, RED = 0, 1
# The learn spells of the plans cast for 3 seconds (a copy of 2760, cast time index 14); the player casts them.
LEARN_WAIT = 5
GOLD = 10000


def set_skill(client, value, maximum):
    """Sets Blacksmithing to `value` of `maximum` (GM .setskill)."""
    client.gm(".setskill %d %d %d" % (BLACKSMITHING_SKILL, value, maximum), select=client.guid)
    client.pump(1)


def offered_states(client, trainer, spells):
    """{learn spell: (state, required skill, skill value)} of `spells` that the trainer offers, or None when
    its window does not open."""
    offered = client.trainer_list(trainer)
    if offered is None:
        return None
    return {spell: offered[spell] for spell in spells if spell in offered}


def check_offer(client, trainer, who, spells, skill):
    """The trainer offers `spells` at Blacksmithing `skill`: red one point below it, green at it."""
    set_skill(client, skill - 1, skill)
    below = offered_states(client, trainer, spells)
    results.check("%s opens its trainer window" % who, below is not None)
    results.check("%s offers the %d recipes" % (who, len(spells)), below is not None and set(below) == set(spells),
                  "offered %r" % sorted(below or {}))
    results.check("%s teaches them at Blacksmithing %d" % (who, skill),
                  below is not None and all(state[1:] == (BLACKSMITHING_SKILL, skill) for state in below.values()),
                  "got %r" % below)
    results.check("at skill %d they are red" % (skill - 1),
                  below is not None and all(state[0] == RED for state in below.values()), "got %r" % below)
    set_skill(client, skill, skill)
    at = offered_states(client, trainer, spells)
    results.check("at skill %d they are green" % skill,
                  at is not None and set(at) == set(spells) and all(state[0] == GREEN for state in at.values()),
                  "got %r" % at)


def check_buy(client, trainer, learn, recipe, name):
    """Buys the learn spell at the trainer, as the Train button does, and waits out its cast: the server answers
    with success, and the character knows the recipe."""
    answers = client.buy_spell(trainer, learn, wait=LEARN_WAIT)
    results.check("buying %s at the trainer succeeds" % name, answers == [True], "answers %r" % answers)
    results.check("the character knows %s (%d)" % (name, recipe), client.knows(recipe), "learned %r" % client.learned)


mode = sys.argv[1] if len(sys.argv) > 1 else ""
if mode not in ("forever", "vanilla"):
    sys.exit("usage: t239_bs_trainers.py forever|vanilla")

realm = Realm()
results = Results()
name = "Trainsmith" if mode == "forever" else "Trainplain"
c = realm.login("PLAYER239", name, race=2, cls=3, client_class=TrainerClient)
me = c.guid
clear_backpack(realm, c)
# A run again starts as a character with no Blacksmithing: losing the profession takes its recipes away.
c.gm(".unlearn %d" % BLACKSMITHING_APPRENTICE, select=me)
c.pump(1)
results.check("the character starts without Copper Rod and Rough Copper Vest",
              not {COPPER_ROD, ROUGH_COPPER_VEST} & saved_spells(realm, c))

# Apprentice Blacksmithing: the recipes that come with the skill.
c.gm(".learn %d" % BLACKSMITHING_APPRENTICE, select=me)
c.pump(2)
spells = saved_spells(realm, c)
results.check("the new Apprentice knows Rough Copper Vest", ROUGH_COPPER_VEST in spells)
if mode == "forever":
    results.check("the new Apprentice knows Copper Rod with no trainer step", COPPER_ROD in spells)
else:
    results.check("the new Apprentice does not know Copper Rod", COPPER_ROD not in spells)
c.gm(".modify money %d" % GOLD, select=me)
c.pump(1)

apprentice = spawn(realm, c, APPRENTICE_TRAINER)
journeyman = spawn(realm, c, JOURNEYMAN_TRAINER)
if mode == "forever":
    check_offer(c, apprentice, "the Apprentice trainer", BOOTS, BOOTS_SKILL)
    check_buy(c, apprentice, STRANGE_COPPER_BOOTS_LEARN, BOOTS[STRANGE_COPPER_BOOTS_LEARN], "Strange Copper Boots")
    set_skill(c, BELTS_SKILL, BELTS_SKILL)
    belts = offered_states(c, apprentice, BELTS)
    results.check("the Apprentice trainer offers no belt (its rank caps below 150)", belts == {}, "got %r" % belts)
    check_offer(c, journeyman, "the Journeyman trainer", BELTS, BELTS_SKILL)
    check_buy(c, journeyman, OFFICERS_BELT_LEARN, BELTS[OFFICERS_BELT_LEARN], "Officer's Belt")
else:
    set_skill(c, BELTS_SKILL, BELTS_SKILL)
    for who, trainer in (("the Apprentice trainer", apprentice), ("the Journeyman trainer", journeyman)):
        offered = offered_states(c, trainer, list(BOOTS) + list(BELTS))
        results.check("%s offers no new learn spell" % who, offered == {}, "got %r" % offered)
for trainer in (apprentice, journeyman):
    c.gm(".npc spawn delete", select=trainer)

results.finish()
