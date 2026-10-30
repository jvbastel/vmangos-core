#!/usr/bin/env python3
"""In-game check of jvbastel/WOWAI#226, the tracer bullet of the Forever professions (spec #225), on the check kit.

    # A slot with the Forever rules:
    wowai/test-realm/realm.sh --ticket N acquire --forever
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t226_forever_recipe.py forever
    # A slot without them:
    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t226_forever_recipe.py vanilla

An Orc Hunter (not a Warrior) with Blacksmithing gets the reagents and a Blacksmith Hammer, and a GM Anvil
at its feet. The recipe and its item come from forever/spec/blacksmithing.tsv and forever/spec/items.tsv:
Strange Copper Boots, recipe spell 61000, item 70000 (Forever client build 1.60.1.70685: recipe 1252230,
item 250621).

forever: a GM teaches the recipe, the character crafts it at the Anvil, the boots are in its bags, and the
         item query (the tooltip data of the client) has the Forever stats: rare, item level 15, level 10,
         bind on pickup, 109 armor, Stamina 4, the equip spell Increase Spell Dam 5 (9395).
vanilla: the GM command cannot teach the recipe, the item query finds no item 70000, and a GM cannot add
         the item.
"""
import struct
import sys

from kit import Realm, Results, clear_backpack, saved_spells
from wowclient import SpellClient

RECIPE = 61000
ITEM = 70000
BLACKSMITHING_SKILL, BLACKSMITHING_APPRENTICE = 164, 2018
BLACKSMITH_HAMMER = 5956
REAGENTS = ((2318, 2), (2840, 2), (10940, 4))  # Light Leather, Copper Bar, Strange Dust
ANVIL = 1744  # gameobject_template: type 8 (spell focus), data0 1 (Anvil)
CMSG_ITEM_QUERY_SINGLE, SMSG_ITEM_QUERY_SINGLE_RESPONSE = 86, 88
NOT_FOUND = 0x80000000
STAMINA, ON_EQUIP = 7, 1
EXPECTED = {"name": "Strange Copper Boots", "quality": 3, "item_level": 15, "required_level": 10, "armor": 109,
            "bonding": 1, "display": 23528, "stats": [(STAMINA, 4)], "spells": [(9395, ON_EQUIP)]}


class ItemClient(SpellClient):
    """A SpellClient that keeps the SMSG_ITEM_QUERY_SINGLE_RESPONSE bodies by item entry."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.item_answers = {}

    def _observe(self, opcode, body):
        super()._observe(opcode, body)
        if opcode == SMSG_ITEM_QUERY_SINGLE_RESPONSE and len(body) >= 4:
            self.item_answers[struct.unpack_from("<I", body, 0)[0]] = body

    def query_item(self, entry, wait=1.5):
        """The parsed item query answer of `entry` (as the client asks for a tooltip), or None when the
        server knows no such item."""
        self.item_answers.pop(entry, None)
        self.item_answers.pop(entry | NOT_FOUND, None)
        self.send(CMSG_ITEM_QUERY_SINGLE, struct.pack("<IQ", entry, 0))
        self.pump(wait)
        body = self.item_answers.get(entry)
        return parse_item(body) if body else None


def parse_item(body):
    """The fields of a 1.12 SMSG_ITEM_QUERY_SINGLE_RESPONSE (ItemHandler.cpp, HandleItemQuerySingleOpcode)."""
    pos = 12  # entry, class, subclass
    names = []
    for _ in range(4):
        end = body.index(b"\0", pos)
        names.append(body[pos:end].decode("utf-8", "replace"))
        pos = end + 1
    head = struct.unpack_from("<20I", body, pos)
    pos += 80
    stats = [struct.unpack_from("<Ii", body, pos + 8 * k) for k in range(10)]
    pos += 80 + 5 * 12  # the stats, then 5 x (min, max, school) damage
    armor = struct.unpack_from("<I", body, pos)[0]
    pos += 7 * 4 + 4 + 4 + 4  # armor and 6 resistances, delay, ammo type, ranged range
    spells = [struct.unpack_from("<II", body, pos + 24 * k) for k in range(5)]
    pos += 5 * 24
    bonding = struct.unpack_from("<I", body, pos)[0]
    return {"name": names[0], "display": head[0], "quality": head[1], "item_level": head[8],
            "required_level": head[9], "armor": armor, "bonding": bonding,
            "stats": [s for s in stats if s != (0, 0)], "spells": [s for s in spells if s[0]]}


def items_in_bags(realm, client, entry):
    client.gm(".save", select=client.guid)
    client.pump(1)
    return int(realm.sql("characters", "SELECT COUNT(*) FROM character_inventory WHERE guid = %d AND item_id = %d"
                         % (client.guid, entry)) or 0)


mode = sys.argv[1] if len(sys.argv) > 1 else ""
if mode not in ("forever", "vanilla"):
    sys.exit("usage: t226_forever_recipe.py forever|vanilla")

realm = Realm()
results = Results()
name = "Forgeswift" if mode == "forever" else "Forgeplain"
c = realm.login("PLAYER226", name, race=2, cls=3, client_class=ItemClient)
me = c.guid
clear_backpack(realm, c)
c.gm(".learn %d" % BLACKSMITHING_APPRENTICE, select=me)
c.gm(".setskill %d 40 75" % BLACKSMITHING_SKILL, select=me)
skill = realm.sql("characters", "SELECT value FROM character_skills WHERE guid = %d AND skill = %d"
                  % (me, BLACKSMITHING_SKILL))
c.gm(".learn %d" % RECIPE, select=me)
c.pump(1)

if mode == "forever":
    results.check("the Hunter has Blacksmithing", c.knows(BLACKSMITHING_APPRENTICE), "skill %r" % skill)
    results.check("a GM teaches the Forever recipe", c.knows(RECIPE), "learned %r" % c.learned)
    results.check("the recipe is saved", RECIPE in saved_spells(realm, c))
    before = items_in_bags(realm, c, ITEM)
    c.gm(".additem %d" % BLACKSMITH_HAMMER, select=me)
    for reagent, count in REAGENTS:
        c.gm(".additem %d %d" % (reagent, count), select=me)
    c.gm(".gobject tmpadd %d 600" % ANVIL, select=me)
    reasons, went = c.cast(RECIPE, wait=6)
    results.check("the character crafts the recipe at the Anvil", went and not any(reasons),
                  "fail reasons %r" % reasons)
    results.check("the crafted Strange Copper Boots are in the bags", items_in_bags(realm, c, ITEM) == before + 1,
                  "before %d" % before)
    results.check("the reagents are used up",
                  all(items_in_bags(realm, c, reagent) == 0 for reagent, _ in REAGENTS))
    item = c.query_item(ITEM)
    results.check("the server answers the item query of the boots", item is not None)
    for key, value in EXPECTED.items():
        results.check("the item has its Forever %s" % key, item is not None and item[key] == value,
                      "got %r" % (item or {}).get(key))
else:
    results.check("the GM command cannot teach the recipe", not c.knows(RECIPE), "learned %r" % c.learned)
    results.check("the character has no recipe row", RECIPE not in saved_spells(realm, c, enabled_only=False))
    results.check("the server knows no item %d" % ITEM, c.query_item(ITEM) is None)
    c.gm(".additem %d" % ITEM, select=me)
    results.check("a GM cannot add the item", items_in_bags(realm, c, ITEM) == 0)

results.finish()
