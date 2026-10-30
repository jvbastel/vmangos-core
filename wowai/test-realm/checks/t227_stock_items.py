#!/usr/bin/env python3
"""In-game check of jvbastel/WOWAI#227, the Forever changes to stock crafted items (spec #225), on the check kit.

    # A slot with the Forever rules:
    wowai/test-realm/realm.sh --ticket N acquire --forever
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t227_stock_items.py forever
    # A slot without them:
    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t227_stock_items.py vanilla

An Orc Rogue (not a Warrior) asks the server for the item data of four changed stock items, as the client
does for a tooltip, and wears two Copper Shortswords. The Forever values are the `stock` rows of
forever/spec/items.tsv (Forever client build 1.60.1.70685); the vanilla values are the stock 1.12 rows.

- Copper Shortsword (2847): Main Hand -> One-Hand, speed 2.1 -> 2.5, white -> green, Str 1, bind on equip.
  With the rules on, the second sword goes into the off hand; with them off, it cannot (a Main Hand weapon).
- Golden Scale Coif (3837): Spirit 10 -> Stamina 10.
- Thorium Armor (12405): gains Stamina 15 and keeps its resistances.
- Annihilator (12798): Main Hand -> One-Hand, speed 1.7 -> 2.4, gains Attack Power 14 (9142) and keeps its
  chance on hit Armor Shatter (16928).
"""
import struct
import sys

from kit import Realm, Results, clear_backpack
from wowclient import CMSG_DESTROYITEM, SpellClient

CMSG_ITEM_QUERY_SINGLE, SMSG_ITEM_QUERY_SINGLE_RESPONSE = 86, 88
CMSG_AUTOEQUIP_ITEM, CMSG_SWAP_INV_ITEM = 266, 269
BACKPACK, MAIN_HAND_SLOT, OFF_HAND_SLOT = 255, 15, 16
NOT_FOUND = 0x80000000
ONE_HANDED_SWORDS, DUAL_WIELD = 201, 674
STR, SPI, STA = 4, 6, 7
ON_EQUIP, CHANCE_ON_HIT = 1, 2
ONE_HAND, MAIN_HAND = 13, 21
COPPER_SHORTSWORD, GOLDEN_SCALE_COIF, THORIUM_ARMOR, ANNIHILATOR = 2847, 3837, 12405, 12798
LEVEL = 10

EXPECTED = {
    "forever": {
        COPPER_SHORTSWORD: {"quality": 2, "inventory_type": ONE_HAND, "delay": 2500, "damage": (9, 18),
                            "stats": [(STR, 1)], "bonding": 2, "item_level": 10},
        GOLDEN_SCALE_COIF: {"stats": [(STR, 10), (STA, 10)]},
        THORIUM_ARMOR: {"stats": [(STA, 15)], "resistances": [0, 8, 8, 8, 8, 8]},
        ANNIHILATOR: {"inventory_type": ONE_HAND, "delay": 2400, "damage": (69, 129),
                      "spells": [(16928, CHANCE_ON_HIT), (9142, ON_EQUIP)]},
    },
    "vanilla": {
        COPPER_SHORTSWORD: {"quality": 1, "inventory_type": MAIN_HAND, "delay": 2100, "damage": (5, 11),
                            "stats": [], "bonding": 0, "item_level": 9},
        GOLDEN_SCALE_COIF: {"stats": [(STR, 10), (SPI, 10)]},
        THORIUM_ARMOR: {"stats": [], "resistances": [0, 8, 8, 8, 8, 8]},
        ANNIHILATOR: {"inventory_type": MAIN_HAND, "delay": 1700, "damage": (49, 92),
                      "spells": [(16928, CHANCE_ON_HIT)]},
    },
}


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
    head = struct.unpack_from("<20I", body, pos)  # display, quality, ..., inventory type (5), item level (8)
    pos += 80
    stats = [struct.unpack_from("<Ii", body, pos + 8 * k) for k in range(10)]
    pos += 80
    damage = struct.unpack_from("<ff", body, pos)  # the first of 5 x (min, max, school)
    pos += 5 * 12
    armor_and_resistances = struct.unpack_from("<7I", body, pos)
    pos += 7 * 4
    delay = struct.unpack_from("<I", body, pos)[0]
    pos += 4 + 4 + 4  # delay, ammo type, ranged range
    spells = [struct.unpack_from("<II", body, pos + 24 * k) for k in range(5)]
    pos += 5 * 24
    bonding = struct.unpack_from("<I", body, pos)[0]
    return {"name": names[0], "quality": head[1], "inventory_type": head[5], "item_level": head[8],
            "stats": [s for s in stats if s != (0, 0)], "damage": tuple(int(round(d)) for d in damage),
            "resistances": list(armor_and_resistances[1:]), "delay": delay,
            "spells": [s for s in spells if s[0]], "bonding": bonding}


def item_in(slot):
    c.gm(".save", select=me)
    row = realm.sql("characters", "SELECT item_id FROM character_inventory WHERE guid = %d AND bag = 0 AND slot = %d"
                    % (me, slot))
    return int(row) if row else 0


def backpack_slot_of(entry):
    c.gm(".save", select=me)
    row = realm.sql("characters", "SELECT slot FROM character_inventory WHERE guid = %d AND bag = 0 AND slot >= 23 "
                    "AND item_id = %d ORDER BY slot LIMIT 1" % (me, entry))
    return int(row.split()[0]) if row else None


def empty_hands():
    """Destroys what the hands hold, so that the check can run again on the same character."""
    for slot in (MAIN_HAND_SLOT, OFF_HAND_SLOT):
        c.send(CMSG_DESTROYITEM, struct.pack("<BBBBBB", BACKPACK, slot, 0, 0, 0, 0))
    c.pump(1)


mode = sys.argv[1] if len(sys.argv) > 1 else ""
if mode not in ("forever", "vanilla"):
    sys.exit("usage: t227_stock_items.py forever|vanilla")

realm = Realm()
results = Results()
name = "Stockhone" if mode == "forever" else "Stockdull"
c = realm.login("PLAYER227", name, race=2, cls=4, client_class=ItemClient)
me = c.guid
level = int(realm.sql("characters", "SELECT level FROM characters WHERE guid = %d" % me) or 1)
if level < LEVEL:
    c.gm(".levelup %d" % (LEVEL - level), select=me)
for spell in (ONE_HANDED_SWORDS, DUAL_WIELD):
    c.gm(".learn %d" % spell, select=me)
clear_backpack(realm, c)
empty_hands()

for entry, expected in EXPECTED[mode].items():
    item = c.query_item(entry)
    results.check("the server answers the item query of %d" % entry, item is not None)
    for key, value in expected.items():
        results.check("%s: item %d has its %s %s" % (mode, entry, mode, key), item is not None and item[key] == value,
                      "got %r, want %r" % ((item or {}).get(key), value))

# Two Copper Shortswords: the first into the main hand, the second dragged to the off hand.
c.gm(".additem %d 2" % COPPER_SHORTSWORD, select=me)
first = backpack_slot_of(COPPER_SHORTSWORD)
if first is not None:
    c.send(CMSG_AUTOEQUIP_ITEM, struct.pack("<BB", BACKPACK, first))
    c.pump(1.5)
results.check("a Copper Shortsword is in the main hand", item_in(MAIN_HAND_SLOT) == COPPER_SHORTSWORD)
second = backpack_slot_of(COPPER_SHORTSWORD)
if second is not None:
    c.send(CMSG_SWAP_INV_ITEM, struct.pack("<BB", second, OFF_HAND_SLOT))
    c.pump(1.5)
off_hand = item_in(OFF_HAND_SLOT)
if mode == "forever":
    results.check("forever: the former Main Hand sword goes into the off hand", off_hand == COPPER_SHORTSWORD,
                  "off hand %r, backpack slot %r" % (off_hand, second))
else:
    results.check("vanilla: the Main Hand sword cannot go into the off hand", off_hand == 0 and second is not None,
                  "off hand %r, backpack slot %r" % (off_hand, second))

results.finish()
