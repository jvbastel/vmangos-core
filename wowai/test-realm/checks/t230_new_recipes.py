#!/usr/bin/env python3
"""In-game check of jvbastel/WOWAI#230, the new Forever Blacksmithing recipes (spec #225), on the check kit.

    # A slot with the Forever rules:
    wowai/test-realm/realm.sh --ticket N acquire --forever
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t230_new_recipes.py forever
    # A slot without them:
    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t230_new_recipes.py vanilla

An Orc Hunter (not a Warrior) with Blacksmithing 300 and a Blacksmith Hammer stands at a GM Anvil. It takes
one recipe of each family from forever/spec/blacksmithing.tsv, and the values of the item it makes from
forever/spec/items.tsv (Forever client build 1.60.1.70685):
  a plate profile (Officer's Boots), a mail profile (Guard's Boots, with the new reagent Pristine Leather),
  a hybrid strength and spell power piece (Sterling Silver Boots), a fist weapon (Brass Knuckles), a polearm
  (Bronze Dory) and a thrown hammer (Cracked Blacksmith Hammer).

forever: a GM teaches each recipe, the character crafts it at the Anvil with its reagents, the item is in its
         bags, and the item query (the tooltip data of the client) has the Forever values of the spec row:
         name, quality, item level, required level, bonding, armor, base stats, the equip spell of each bonus,
         and the damage and speed of a weapon. The plans of one recipe (Officer's Boots) are a known item that
         needs Blacksmithing at the plans skill.
vanilla: the GM command teaches none of the recipes, and the server knows none of the items.
"""
import struct
import sys
from pathlib import Path

from kit import Realm, Results, clear_backpack, saved_spells
from wowclient import SpellClient

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "forever" / "generator"))
from forever_gen import equip_stats  # noqa: E402  (the bonus -> stock equip spell table of the spec)

SPEC = ROOT / "forever" / "spec"
FAMILIES = ("Officer's Boots", "Guard's Boots", "Sterling Silver Boots", "Brass Knuckles", "Bronze Dory",
            "Cracked Blacksmith Hammer")
PLANS_OF = "Officer's Boots"
BLACKSMITHING_SKILL, BLACKSMITHING_ARTISAN = 164, 9785
BLACKSMITH_HAMMER = 5956
ANVIL = 1744  # gameobject_template: type 8 (spell focus), data0 1 (Anvil)
CMSG_ITEM_QUERY_SINGLE, SMSG_ITEM_QUERY_SINGLE_RESPONSE = 86, 88
NOT_FOUND = 0x80000000
ON_EQUIP = 1
QUALITIES = {"poor": 0, "common": 1, "uncommon": 2, "rare": 3, "epic": 4}
BONDINGS = {"none": 0, "pickup": 1, "equip": 2, "use": 3}
STAT_TYPES = {"Mana": 0, "Health": 1, "Agi": 3, "Str": 4, "Int": 5, "Spi": 6, "Sta": 7}


def spec_rows(name):
    lines = [line for line in (SPEC / name).read_text(encoding="utf-8").splitlines()
             if line and not line.startswith("#")]
    header = lines[0].split("\t")
    return [dict(zip(header, line.split("\t"))) for line in lines[1:]]


def counted(text):
    return [tuple(int(v) for v in part.split("x")) for part in text.split(",")]


def expected_item(row):
    """The item query values of a spec item row."""
    stats = [] if row["stats"] == "-" else [s.strip().split(" ") for s in row["stats"].split(",")]
    bonuses = equip_stats.parse(row["name"], row["bonuses"])
    expected = {"name": row["name"], "quality": QUALITIES[row["quality"]], "item_level": int(row["itemLevel"]),
                "required_level": int(row["requiredLevel"]), "bonding": BONDINGS[row["bonding"]],
                "armor": int(row["armor"]), "stats": [(STAT_TYPES[n], int(v)) for n, v in stats],
                "spells": [(b.spell, ON_EQUIP) for b in bonuses], "display": int(row["displayId"])}
    if row["damage"] != "-":
        low, high = row["damage"].split("-")
        expected.update(damage=(int(low), int(high)), delay=int(row["delay"]))
    return expected


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
        """The parsed item query answer of `entry` (as the client asks for a tooltip), or None."""
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
    # display, quality, flags, buy price, sell price, inventory type, classes, races, item level (8),
    # required level (9), required skill (10), required skill rank (11), ...
    head = struct.unpack_from("<20I", body, pos)
    pos += 80
    stats = [struct.unpack_from("<Ii", body, pos + 8 * k) for k in range(10)]
    pos += 80
    damage = struct.unpack_from("<ff", body, pos)  # the first of 5 x (min, max, school)
    pos += 5 * 12
    armor = struct.unpack_from("<I", body, pos)[0]
    pos += 7 * 4
    delay = struct.unpack_from("<I", body, pos)[0]
    pos += 4 + 4 + 4  # delay, ammo type, ranged range
    spells = [struct.unpack_from("<II", body, pos + 24 * k) for k in range(5)]
    pos += 5 * 24
    bonding = struct.unpack_from("<I", body, pos)[0]
    return {"name": names[0], "display": head[0], "quality": head[1], "item_level": head[8],
            "required_level": head[9], "required_skill": (head[10], head[11]), "armor": armor,
            "stats": [s for s in stats if s != (0, 0)], "damage": tuple(int(round(d)) for d in damage),
            "delay": delay, "spells": [s for s in spells if s[0]], "bonding": bonding}


def items_in_bags(realm, client, entry):
    client.gm(".save", select=client.guid)
    client.pump(1)
    return int(realm.sql("characters", "SELECT COUNT(*) FROM character_inventory WHERE guid = %d AND item_id = %d"
                         % (client.guid, entry)) or 0)


mode = sys.argv[1] if len(sys.argv) > 1 else ""
if mode not in ("forever", "vanilla"):
    sys.exit("usage: t230_new_recipes.py forever|vanilla")

recipes = {row["name"]: row for row in spec_rows("blacksmithing.tsv") if row["kind"] == "recipe"}
items = {int(row["entry"]): row for row in spec_rows("items.tsv") if row["kind"] == "item"}

realm = Realm()
results = Results()
name = "Anvilsix" if mode == "forever" else "Anvilnone"
c = realm.login("PLAYER230", name, race=2, cls=3, client_class=ItemClient)
me = c.guid
clear_backpack(realm, c)
c.gm(".learn %d" % BLACKSMITHING_ARTISAN, select=me)
c.gm(".setskill %d 300 300" % BLACKSMITHING_SKILL, select=me)
for family in FAMILIES:
    c.gm(".learn %s" % recipes[family]["spell"], select=me)
c.pump(1)

if mode == "forever":
    results.check("the Hunter has Blacksmithing", c.knows(BLACKSMITHING_ARTISAN))
    c.gm(".additem %d" % BLACKSMITH_HAMMER, select=me)
    c.gm(".gobject tmpadd %d 900" % ANVIL, select=me)
    for family in FAMILIES:
        recipe = recipes[family]
        spell = int(recipe["spell"])
        (made, _), = counted(recipe["creates"])
        results.check("a GM teaches %s (%d)" % (family, spell), c.knows(spell) and spell in saved_spells(realm, c),
                      "learned %r" % c.learned)
        before = items_in_bags(realm, c, made)
        for reagent, count in counted(recipe["reagents"]):
            c.gm(".additem %d %d" % (reagent, count), select=me)
        reasons, went = c.cast(spell, wait=6)
        results.check("the character crafts %s at the Anvil" % family, went and not any(reasons),
                      "fail reasons %r" % reasons)
        results.check("the crafted %s is in the bags" % family, items_in_bags(realm, c, made) == before + 1,
                      "before %d" % before)
        results.check("the reagents of %s are used up" % family,
                      all(items_in_bags(realm, c, reagent) == 0 for reagent, _ in counted(recipe["reagents"])))
        item = c.query_item(made)
        results.check("the server answers the item query of %s" % family, item is not None)
        for key, value in expected_item(items[made]).items():
            results.check("%s has its Forever %s" % (family, key), item is not None and item[key] == value,
                          "got %r, want %r" % ((item or {}).get(key), value))
    plans, _ = recipes[PLANS_OF]["plans"].split("/")
    answer = c.query_item(int(plans))
    want = (BLACKSMITHING_SKILL, int(recipes[PLANS_OF]["minRank"]))
    results.check("the plans of %s are an item that needs Blacksmithing %d" % (PLANS_OF, want[1]),
                  answer is not None and answer["required_skill"] == want, "got %r" % (answer or {}).get("required_skill"))
else:
    for family in FAMILIES:
        spell = int(recipes[family]["spell"])
        (made, _), = counted(recipes[family]["creates"])
        results.check("the GM command cannot teach %s" % family, not c.knows(spell), "learned %r" % c.learned)
        results.check("the server knows no item %d (%s)" % (made, family), c.query_item(made) is None)

results.finish()
