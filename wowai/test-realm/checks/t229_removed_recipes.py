#!/usr/bin/env python3
"""In-game check of jvbastel/WOWAI#229, the removal marker and the six removed Blacksmithing recipes (spec
#225), on the check kit.

    # A slot with the Forever rules:
    wowai/test-realm/realm.sh --ticket N acquire --forever
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t229_removed_recipes.py forever
    # A slot without them:
    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t229_removed_recipes.py vanilla

The skill line: the server gives a craft skill-up only by the skill line ability of the recipe
(Player::UpdateCraftSkill). An Orc Hunter with Blacksmithing 250 (of 300), who knows Storm Gauntlets
(16661, stock colours 315-335, so orange at 250: a skill-up on every craft), crafts them at a GM Anvil.
(Not Heavy Timbermaw Belt: its item comes with a later content patch than the test realm loads, so the
server calls the spell broken and does not teach it.)
forever: the gauntlets are crafted (the spell stays), and the skill stays at 250: the server's skill line
         does not hold the recipe. The world DB marks every build row of its skill line ability (9579).
vanilla: the gauntlets are crafted and the skill goes up to 251: the stock skill line ability loads as usual.

The trainers: the trainer window of a Blacksmithing trainer that a GM spawns (Saru Steelfury, 3355) offers
no learn spell of the six removed recipes, in both modes. In stock 1.12 no trainer teaches any of the six
(they come from plans), so "with the rules off, the trainers offer it" has no case to show. The trainer
rows go through the same Forever::Table() union and marker as the skill line abilities.
"""
import struct
import sys

from kit import Realm, Results, clear_backpack, spawn
from wowclient import SpellClient

# The six removed recipes (forever/spec/blacksmithing.tsv, kind removed): spell -> learn spell.
REMOVED = {16650: 16679, 16728: 16750, 16746: 16762, 16661: 16692, 23628: 23630, 23629: 23631}
RECIPE, RECIPE_ITEM, RECIPE_ABILITY = 16661, 12632, 9579
# Storm Gauntlets: 20 Thorium Bar, 4 Enchanted Thorium Bar, 4 Essence of Water, 4 Large Opal, at an Anvil.
REAGENTS = [(12359, 20), (12655, 4), (7080, 4), (12361, 4)]
ANVIL = 1744  # gameobject_template: type 8 (spell focus), data0 1 (Anvil)
BLACKSMITHING_SKILL, BLACKSMITHING_APPRENTICE = 164, 2018
BLACKSMITH_HAMMER = 5956
SKILL, MAX_SKILL = 250, 300
SKILL_AFTER = {"forever": SKILL, "vanilla": SKILL + 1}
TRAINER = 3355
CMSG_TRAINER_LIST, SMSG_TRAINER_LIST = 432, 433
# One spell of SMSG_TRAINER_LIST (NPCHandler.cpp, SendTrainerSpellHelper).
TRAINER_SPELL = struct.Struct("<IBIIIBIIIII")


class TrainerClient(SpellClient):
    """A SpellClient that keeps the spells of the last SMSG_TRAINER_LIST (as in t228_recipe_shifts.py)."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.trainer_spells = None

    def _observe(self, opcode, body):
        super()._observe(opcode, body)
        if opcode == SMSG_TRAINER_LIST and len(body) >= 16:
            count = struct.unpack_from("<I", body, 12)[0]
            spells = set()
            for k in range(count):
                if 16 + (k + 1) * TRAINER_SPELL.size > len(body):
                    break
                spells.add(TRAINER_SPELL.unpack_from(body, 16 + k * TRAINER_SPELL.size)[0])
            self.trainer_spells = spells

    def trainer_list(self, guid, wait=1.5):
        self.trainer_spells = None
        self.send(CMSG_TRAINER_LIST, struct.pack("<Q", guid))
        self.pump(wait)
        return self.trainer_spells


def saved_count(realm, client, query):
    client.gm(".save", select=client.guid)
    client.pump(1)
    return int(realm.sql("characters", query % client.guid) or 0)


mode = sys.argv[1] if len(sys.argv) > 1 else ""
if mode not in ("forever", "vanilla"):
    sys.exit("usage: t229_removed_recipes.py forever|vanilla")

realm = Realm()
results = Results()
name = "Gonesmith" if mode == "forever" else "Goneplain"
c = realm.login("PLAYER229", name, race=2, cls=3, client_class=TrainerClient)
me = c.guid
clear_backpack(realm, c)
c.gm(".learn %d" % BLACKSMITHING_APPRENTICE, select=me)
c.gm(".setskill %d %d %d" % (BLACKSMITHING_SKILL, SKILL, MAX_SKILL), select=me)
c.gm(".learn %d" % RECIPE, select=me)
c.pump(1)
skill_query = "SELECT value FROM character_skills WHERE guid = %%d AND skill = %d" % BLACKSMITHING_SKILL
results.check("the character has Blacksmithing %d" % SKILL, saved_count(realm, c, skill_query) == SKILL)
results.check("the character knows Storm Gauntlets (the spell stays)", c.knows(RECIPE), "learned %r" % c.learned)

if mode == "forever":
    # One row per client build: all of them are marked.
    marked = realm.sql("mangos", "SELECT COUNT(*) > 0 AND SUM(forever_removed = 0) = 0 FROM forever_skill_line_ability"
                       " WHERE id = %d" % RECIPE_ABILITY)
    results.check("the world DB marks every build row of skill line ability %d" % RECIPE_ABILITY, marked == "1",
                  "got %r" % marked)

# The skill line: a craft at orange gives a skill-up only through the recipe's skill line ability.
c.gm(".additem %d" % BLACKSMITH_HAMMER, select=me)
for reagent, count in REAGENTS:
    c.gm(".additem %d %d" % (reagent, count), select=me)
c.gm(".gobject tmpadd %d 600" % ANVIL, select=me)
c.pump(1)
before = saved_count(realm, c, "SELECT COUNT(*) FROM character_inventory WHERE guid = %%d AND item_id = %d" % RECIPE_ITEM)
# Storm Gauntlets cast for 60 seconds (SMSG_SPELL_START, cast time index 39).
reasons, went = c.cast(RECIPE, wait=65)
made = saved_count(realm, c, "SELECT COUNT(*) FROM character_inventory WHERE guid = %%d AND item_id = %d" % RECIPE_ITEM) - before
results.check("the character crafts Storm Gauntlets at the Anvil", went and not any(reasons) and made == 1,
              "fail reasons %r, made %d" % (reasons, made))
skill = saved_count(realm, c, skill_query)
results.check("after the craft the skill is %d (%s)" % (SKILL_AFTER[mode], "no skill line ability" if mode == "forever"
                                                       else "the stock skill line ability"),
              skill == SKILL_AFTER[mode], "got %d" % skill)

# The trainers: no Blacksmithing trainer offers a removed recipe.
trainer = spawn(realm, c, TRAINER)
offered = c.trainer_list(trainer)
results.check("the Blacksmithing trainer opens its trainer window", offered is not None)
results.check("the trainer offers no removed recipe", offered is not None and not offered & set(REMOVED.values()),
              "offered %r" % sorted((offered or set()) & set(REMOVED.values())))
c.gm(".npc delete", select=trainer)
learned_rows = realm.sql("mangos", "SELECT COUNT(*) FROM npc_trainer WHERE spell IN (%s)"
                         % ", ".join(str(s) for s in REMOVED.values()))
results.check("no stock trainer row teaches a removed recipe", learned_rows == "0", "got %r" % learned_rows)

results.finish()
