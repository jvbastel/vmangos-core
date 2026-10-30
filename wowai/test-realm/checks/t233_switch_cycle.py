#!/usr/bin/env python3
"""In-game check of jvbastel/WOWAI#233 (spec #225): the rules switch keeps Forever recipes and Forever-only
items for every class, through a full off-and-on cycle with a restart of the realm between the steps. Each step
is one run on the same slot; an acquire between them restarts the slot with the other rules and keeps the
database (same lease), as in t208_rules_switch.py:

    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t233_switch_cycle.py setup
    wowai/test-realm/realm.sh --ticket N acquire --forever
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t233_switch_cycle.py forever
    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t233_switch_cycle.py vanilla
    wowai/test-realm/realm.sh --ticket N acquire --forever
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t233_switch_cycle.py forever-again

Two characters with Blacksmithing: an Orc Hunter (not a Warrior) and an Orc Warrior. The Forever recipe is
Strange Copper Boots (spell 61000) and the Forever-only item its boots (item 70000, forever/spec/). The removed
stock recipe is Heavy Timbermaw Belt (23628, a `removed` row of blacksmithing.tsv, #229).

setup         (rules off) Both learn Blacksmithing; the Hunter learns Heavy Timbermaw Belt.
forever       (rules on) The Hunter does not have the removed belt recipe, but its row stays. Both learn the
              Forever recipe and get the Forever boots. The Warrior (switched to the Forever rules) reaches
              level 30 and learns the Forever-only talent Lingering Rage (60106), a free-range Warrior spell.
              Then the check asks the character DB cleaner to run at the next startup (all cleaning flags).
vanilla       (rules off) The cleaner ran and left the kept rows. The Hunter logs in: the Forever recipe and
              the boots are not loaded (no recipe in the login spells, the server log names the kept item),
              and both rows are still in the character database after a save; the belt recipe is back. The
              Warrior switches back: Lingering Rage is gone as before (#208), the Forever recipe row and the
              boots row stay. The cleaner is asked to run again.
forever-again (rules on) The cleaner ran and left the rows. Both characters have the recipe and the boots back
              (the boots are loaded: destroying them takes the row away), and the Hunter's belt recipe is
              hidden again.
"""
import struct
import sys

from kit import Realm, Results, clear_backpack, saved_spells
import wowclient
from wowclient import SpellClient

HUNTER_ACCOUNT, HUNTER = "PLAYER233", "Keepsmith"
WARRIOR_ACCOUNT, WARRIOR = "PLAYER233B", "Keepwar"
RECIPE = 61000           # Strange Copper Boots, a Forever recipe (profession spell block 61000-61999)
ITEM = 70000             # Strange Copper Boots, a Forever-only item (item range 70000-79999)
REMOVED_RECIPE = 23628   # Heavy Timbermaw Belt, a stock recipe that the Forever rules remove
BLACKSMITHING_SKILL, BLACKSMITHING_APPRENTICE = 164, 2018
LEVEL = 30
LINGERING_RAGE, LINGERING_RAGE_1 = 60002, 60106  # a Forever-only Warrior talent and its rank 1 spell
ALL_CLEANING_FLAGS = 0x1F  # CharacterDatabaseCleaner.h: characters, pets, items, skills, spells
RULES = {"vanilla": 0, "forever": 1}
SWITCH = r"\[Forever\] %s switches from the %s rules to the %s rules"
KEPT_ITEM = r"\[Forever\] Item %d exists only under the Forever rules" % ITEM
HIDDEN_RECIPE = r"\[Forever\] Recipe %d is removed under the Forever rules" % REMOVED_RECIPE


mode = sys.argv[1] if len(sys.argv) > 1 else ""
if mode not in ("setup", "forever", "vanilla", "forever-again"):
    sys.exit("usage: t233_switch_cycle.py setup|forever|vanilla|forever-again")

realm = Realm()
results = Results()


def rows(query):
    return {int(row) for row in realm.sql("characters", query).split() if row.isdigit()}


def spell_rows(guid):
    """The character_spell rows of a character, enabled or not, as they are now."""
    return rows("SELECT spell FROM character_spell WHERE guid = %d" % guid)


def item_slots(guid):
    """The backpack slots of the Forever boots in character_inventory, as they are now."""
    return rows("SELECT slot FROM character_inventory WHERE guid = %d AND item_id = %d AND bag = 0" % (guid, ITEM))


def item_instance_rows(guid):
    return rows("SELECT guid FROM item_instance WHERE owner_guid = %d AND item_id = %d" % (guid, ITEM))


def stored_rules(guid):
    row = realm.sql("characters", "SELECT rules FROM forever_character WHERE guid = %d" % guid)
    return int(row) if row else None


def ask_cleaner():
    """Asks the character DB cleaner (CharacterDatabaseCleaner, on by default) to run every cleanup at the
    next startup."""
    realm.sql("characters", "UPDATE saved_variables SET cleaning_flags = %d" % ALL_CLEANING_FLAGS)
    flags = realm.sql("characters", "SELECT cleaning_flags FROM saved_variables")
    results.check("the cleaner will run every cleanup at the next startup", flags == str(ALL_CLEANING_FLAGS),
                  "saved_variables.cleaning_flags %r" % flags)


def check_cleaner_ran():
    flags = realm.sql("characters", "SELECT cleaning_flags FROM saved_variables")
    results.check("the character DB cleaner ran at this startup (its flags are reset)", flags == "0", "flags %r" % flags)


def check_rows_kept(who, guid, removed_recipe=False):
    """The kept rows of a logged out character: the Forever recipe, the boots, and the removed stock recipe."""
    spells = spell_rows(guid)
    results.check("%s: the Forever recipe row is in character_spell" % who, RECIPE in spells, "rows %r" % sorted(spells))
    results.check("%s: the Forever boots are in character_inventory" % who, len(item_slots(guid)) == 1,
                  "slots %r" % item_slots(guid))
    results.check("%s: the Forever boots are in item_instance" % who, len(item_instance_rows(guid)) == 1)
    if removed_recipe:
        results.check("%s: the removed stock recipe row is in character_spell" % who, REMOVED_RECIPE in spells)


def login(account, name, cls):
    realm.mark("t233 %s %s" % (mode, name))
    c = realm.login(account, name, race=2, cls=cls, client_class=SpellClient)
    c.pump(2)
    return c


def learn_blacksmithing(c):
    c.gm(".learn %d" % BLACKSMITHING_APPRENTICE, select=c.guid)
    c.gm(".setskill %d 40 75" % BLACKSMITHING_SKILL, select=c.guid)
    c.pump(1)


def get_recipe_and_boots(who, c):
    c.gm(".learn %d" % RECIPE, select=c.guid)
    c.pump(1)
    if not item_slots(c.guid):
        c.gm(".additem %d" % ITEM, select=c.guid)
        c.pump(1)
    results.check("%s learns the Forever recipe" % who, c.knows(RECIPE), "learned %r" % c.learned)
    results.check("%s has the Forever recipe saved" % who, RECIPE in saved_spells(realm, c))
    results.check("%s has the Forever boots in the backpack" % who, len(item_slots(c.guid)) == 1)


def check_not_loaded(who, c):
    """Rules off: the Forever recipe and boots are not loaded, and their rows stay through a save."""
    spells = c.initial_spells
    results.check("%s: the login sends no Forever recipe" % who, spells is not None and RECIPE not in spells,
                  "no SMSG_INITIAL_SPELLS" if spells is None else "")
    results.check("%s: the server keeps the Forever boots without loading them" % who,
                  realm.wait_log(KEPT_ITEM, 5, c))
    c.gm(".save", select=c.guid)
    c.pump(1)
    check_rows_kept(who + " (after a save)", c.guid)


def check_loaded_again(who, c):
    """Rules on again: the recipe is in the spellbook and the boots are loaded (destroying them takes the row)."""
    spells = c.initial_spells
    results.check("%s: the login sends the Forever recipe again" % who, spells is not None and RECIPE in spells,
                  "no SMSG_INITIAL_SPELLS" if spells is None else "")
    results.check("%s: no Forever item is kept unloaded" % who, not realm.wait_log(KEPT_ITEM, 2, c))
    slots = item_slots(c.guid)
    results.check("%s: the Forever boots are in the backpack" % who, len(slots) == 1, "slots %r" % slots)
    if slots:
        c.send(wowclient.CMSG_DESTROYITEM, struct.pack("<BBBBBB", 255, slots.pop(), 0, 0, 0, 0))
        c.pump(1)
        c.gm(".save", select=c.guid)
        c.pump(1)
        results.check("%s: the boots are loaded: destroying them takes their row away" % who, not item_slots(c.guid))


if mode == "setup":
    h = login(HUNTER_ACCOUNT, HUNTER, 3)
    clear_backpack(realm, h)
    learn_blacksmithing(h)
    h.gm(".learn %d" % REMOVED_RECIPE, select=h.guid)
    h.pump(1)
    results.check("the Hunter knows Heavy Timbermaw Belt under the vanilla rules", h.knows(REMOVED_RECIPE),
                  "learned %r" % h.learned)
    results.check("the Hunter's belt recipe is saved", REMOVED_RECIPE in saved_spells(realm, h))
    h.logout()
    w = login(WARRIOR_ACCOUNT, WARRIOR, 1)
    clear_backpack(realm, w)
    learn_blacksmithing(w)
    w.logout()
    results.check("the Warrior last logged in under the vanilla rules", stored_rules(w.guid) == RULES["vanilla"])
elif mode == "forever":
    h = login(HUNTER_ACCOUNT, HUNTER, 3)
    results.check("the Hunter: the login sends no removed belt recipe",
                  h.initial_spells is not None and REMOVED_RECIPE not in h.initial_spells)
    results.check("the server hides the removed belt recipe", realm.wait_log(HIDDEN_RECIPE, 5, h))
    get_recipe_and_boots("the Hunter", h)
    results.check("the Hunter: the removed belt recipe row stays after a save", REMOVED_RECIPE in spell_rows(h.guid))
    h.logout()

    w = login(WARRIOR_ACCOUNT, WARRIOR, 1)
    results.check("the Warrior switches to the Forever rules", realm.wait_log(SWITCH % (WARRIOR, "vanilla", "Forever"), 5, w))
    have = int(realm.sql("characters", "SELECT level FROM characters WHERE guid = %d" % w.guid) or 1)
    if have < LEVEL:
        w.gm(".levelup %d" % (LEVEL - have), select=w.guid)
    w.gm(".reset talents", select=w.guid)
    w.pump(1)
    lingering = w.learn_talent(LINGERING_RAGE)
    results.check("the Warrior learns the Forever-only talent Lingering Rage", lingering == [LINGERING_RAGE_1],
                  "learned %r" % lingering)
    get_recipe_and_boots("the Warrior", w)
    w.logout()
    results.check("the Warrior has Lingering Rage saved", LINGERING_RAGE_1 in spell_rows(w.guid))
    ask_cleaner()
elif mode == "vanilla":
    hunter, warrior = realm.guid_of(HUNTER), realm.guid_of(WARRIOR)
    check_cleaner_ran()
    check_rows_kept("the Hunter (after the cleaner, rules off)", hunter, removed_recipe=True)
    check_rows_kept("the Warrior (after the cleaner, rules off)", warrior)

    h = login(HUNTER_ACCOUNT, HUNTER, 3)
    check_not_loaded("the Hunter", h)
    results.check("the Hunter: the removed belt recipe is back with the rules off",
                  h.initial_spells is not None and REMOVED_RECIPE in h.initial_spells)
    h.logout()

    w = login(WARRIOR_ACCOUNT, WARRIOR, 1)
    results.check("the Warrior switches back to the vanilla rules",
                  realm.wait_log(SWITCH % (WARRIOR, "Forever", "vanilla"), 5, w))
    check_not_loaded("the Warrior", w)
    results.check("the Warrior: Lingering Rage is gone from character_spell, as before",
                  LINGERING_RAGE_1 not in spell_rows(w.guid))
    w.logout()
    results.check("the Warrior: the Forever recipe row stays after the logout", RECIPE in spell_rows(warrior))
    ask_cleaner()
else:
    hunter, warrior = realm.guid_of(HUNTER), realm.guid_of(WARRIOR)
    check_cleaner_ran()
    check_rows_kept("the Hunter (after the cleaner, rules on)", hunter, removed_recipe=True)
    check_rows_kept("the Warrior (after the cleaner, rules on)", warrior)

    h = login(HUNTER_ACCOUNT, HUNTER, 3)
    check_loaded_again("the Hunter", h)
    results.check("the Hunter: the removed belt recipe is hidden again",
                  h.initial_spells is not None and REMOVED_RECIPE not in h.initial_spells)
    h.logout()
    results.check("the Hunter: the removed belt recipe row stays", REMOVED_RECIPE in spell_rows(hunter))

    w = login(WARRIOR_ACCOUNT, WARRIOR, 1)
    results.check("the Warrior switches to the Forever rules again",
                  realm.wait_log(SWITCH % (WARRIOR, "vanilla", "Forever"), 5, w))
    check_loaded_again("the Warrior", w)
    w.logout()

results.finish()
