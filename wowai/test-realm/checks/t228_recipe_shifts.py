#!/usr/bin/env python3
"""In-game check of jvbastel/WOWAI#228, the changed stock Blacksmithing recipes (spec #225), on the check kit.

    # A slot with the Forever rules:
    wowai/test-realm/realm.sh --ticket N acquire --forever
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t228_recipe_shifts.py forever
    # A slot without them:
    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t228_recipe_shifts.py vanilla

An Orc Hunter with Blacksmithing at skill 70 opens the trainer window of a Blacksmithing trainer that a GM
spawns at its feet (Saru Steelfury, 3355, of Orgrimmar), and crafts a Coarse Weightstone with a Coarse Stone
and no Wool Cloth. The values come from forever/spec/blacksmithing.tsv:
Runed Copper Bracers (2664), taught by 2745: trainer skill 90 in 1.12, 65 in Forever (colours -25).
Coarse Weightstone (3116): Coarse Stone and Wool Cloth in 1.12, Coarse Stone only in Forever.

forever: the trainer offers 2745 at skill 65, which the character can learn now (green), and the
         weightstone is crafted from the Coarse Stone alone.
vanilla: the trainer offers 2745 at skill 90 (red), and the weightstone cannot be crafted without the
         Wool Cloth.
"""
import sys

from kit import Realm, Results, clear_backpack, spawn
from wowclient import TrainerClient

TRAINER = 3355
RUNED_COPPER_BRACERS_LEARN = 2745
TRAINER_SKILL = {"forever": 65, "vanilla": 90}
# SMSG_TRAINER_LIST spell states (wowclient.TrainerClient): 0 green (can learn), 1 red (cannot yet).
TRAINER_STATE = {"forever": 0, "vanilla": 1}
COARSE_WEIGHTSTONE, COARSE_WEIGHTSTONE_ITEM = 3116, 3240
COARSE_STONE, WOOL_CLOTH = 2836, 2592
BLACKSMITHING_SKILL, BLACKSMITHING_APPRENTICE = 164, 2018
BLACKSMITH_HAMMER = 5956
SKILL = 70


def items_in_bags(realm, client, entry):
    client.gm(".save", select=client.guid)
    client.pump(1)
    return int(realm.sql("characters", "SELECT COUNT(*) FROM character_inventory WHERE guid = %d AND item_id = %d"
                         % (client.guid, entry)) or 0)


mode = sys.argv[1] if len(sys.argv) > 1 else ""
if mode not in ("forever", "vanilla"):
    sys.exit("usage: t228_recipe_shifts.py forever|vanilla")

realm = Realm()
results = Results()
name = "Shiftsmith" if mode == "forever" else "Shiftplain"
c = realm.login("PLAYER228", name, race=2, cls=3, client_class=TrainerClient)
me = c.guid
clear_backpack(realm, c)
c.gm(".learn %d" % BLACKSMITHING_APPRENTICE, select=me)
c.gm(".setskill %d %d 75" % (BLACKSMITHING_SKILL, SKILL), select=me)
c.pump(1)

# The trainer: the recipe at the Forever (or stock) skill.
trainer = spawn(realm, c, TRAINER)
offered = c.trainer_list(trainer)
results.check("the Blacksmithing trainer opens its trainer window", offered is not None)
bracers = (offered or {}).get(RUNED_COPPER_BRACERS_LEARN)
results.check("the trainer offers Runed Copper Bracers", bracers is not None, "spells %r" % sorted(offered or {}))
results.check("the trainer teaches it at Blacksmithing %d" % TRAINER_SKILL[mode],
              bracers is not None and bracers[1:] == (BLACKSMITHING_SKILL, TRAINER_SKILL[mode]), "got %r" % (bracers,))
results.check("at skill %d the recipe is %s" % (SKILL, "green" if mode == "forever" else "red"),
              bracers is not None and bracers[0] == TRAINER_STATE[mode], "got %r" % (bracers,))
c.gm(".npc delete", select=trainer)

# The weightstone: Coarse Stone and no Wool Cloth.
c.gm(".learn %d" % COARSE_WEIGHTSTONE, select=me)
c.gm(".additem %d" % BLACKSMITH_HAMMER, select=me)
c.gm(".additem %d 1" % COARSE_STONE, select=me)
c.pump(1)
results.check("the character knows Coarse Weightstone", c.knows(COARSE_WEIGHTSTONE), "learned %r" % c.learned)
results.check("the character has no Wool Cloth", items_in_bags(realm, c, WOOL_CLOTH) == 0)
before = items_in_bags(realm, c, COARSE_WEIGHTSTONE_ITEM)
reasons, went = c.cast(COARSE_WEIGHTSTONE, wait=5)
made = items_in_bags(realm, c, COARSE_WEIGHTSTONE_ITEM) - before
if mode == "forever":
    results.check("the weightstone is crafted from the Coarse Stone alone", went and not any(reasons) and made == 1,
                  "fail reasons %r, made %d" % (reasons, made))
    results.check("the Coarse Stone is used up", items_in_bags(realm, c, COARSE_STONE) == 0)
else:
    results.check("the weightstone cannot be crafted without the Wool Cloth", not went and made == 0,
                  "fail reasons %r, made %d" % (reasons, made))
    results.check("the Coarse Stone stays", items_in_bags(realm, c, COARSE_STONE) == 1)

results.finish()
