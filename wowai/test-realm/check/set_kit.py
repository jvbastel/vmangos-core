"""The item set side of the check kit (#231, #248): a character wears set pieces as a player does and reads the set
bonuses as its auras.

    from set_kit import Wearer, set_bonuses
    w = Wearer(realm, client, results)
    w.undress()                                   # no piece worn, an empty backpack
    w.wear(10399, 10403)                          # GM .additem, then CMSG_AUTOEQUIP_ITEM; True when each was made
    w.check_bonuses("Defias", set_bonuses("loot_item_sets.tsv", 161), 2)

set_bonuses reads the spec with the generator's own reader (forever/generator/forever_gen).
"""
import re
import struct
import sys
from pathlib import Path

from kit import clear_backpack
from wowclient import CMSG_DESTROYITEM

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "forever" / "generator"))
from forever_gen import item_sets, spec  # noqa: E402  (the reader of the item set spec files)

CMSG_AUTOEQUIP_ITEM = 266
BACKPACK, EQUIPMENT_SLOTS = 255, 19


def set_bonuses(file_name, set_id):
    """{threshold: [spell]} of the ported bonus rows of the set in forever/spec/<file_name> (item_sets.tsv, or the
    dungeon file loot_item_sets.tsv)."""
    path = ROOT / "forever" / "spec" / file_name
    reader = item_sets.read_dungeon_rows if file_name.startswith("loot_") else item_sets.read_rows
    rows = reader(str(path), spec.data_lines(path.read_text(encoding="utf-8")))["item_sets"]
    found = {}
    for spell, threshold in item_sets.spells(next(s for s in rows if s.set_id == set_id)):
        found.setdefault(threshold, []).append(spell)
    return found


class Wearer:
    """The client's character wears and takes off items, and checks its set bonus auras into `results`."""

    def __init__(self, realm, client, results):
        self.realm, self.client, self.results = realm, client, results
        self.me = client.guid

    def auras(self):
        """The spell IDs of the auras on the character (`.list auras`)."""
        found = set()
        for line in self.client.gm(".list auras", select=self.me, wait=1.0):
            m = re.search(r"id: (\d+) eff: \d+ type: \d+", line)
            if m:
                found.add(int(m.group(1)))
        return found

    def equipped(self):
        """The item IDs in the equipment slots, after a `.save`."""
        self.client.gm(".save", select=self.me)
        rows = self.realm.sql("characters", "SELECT item_id FROM character_inventory WHERE guid = %d AND bag = 0 AND "
                              "slot < %d" % (self.me, EQUIPMENT_SLOTS))
        return {int(row) for row in rows.split() if row.isdigit()}

    def backpack_slot_of(self, entry):
        """The first backpack slot that holds the item `entry`, or None."""
        self.client.gm(".save", select=self.me)
        row = self.realm.sql("characters", "SELECT slot FROM character_inventory WHERE guid = %d AND bag = 0 AND "
                             "slot >= 23 AND item_id = %d ORDER BY slot LIMIT 1" % (self.me, entry))
        return int(row.split()[0]) if row else None

    def wear(self, *entries):
        """Adds each item and equips it from the backpack. True when the server made each item."""
        made = True
        for entry in entries:
            self.client.gm(".additem %d" % entry, select=self.me)
            slot = self.backpack_slot_of(entry)
            if slot is None:
                made = False
                continue
            self.client.send(CMSG_AUTOEQUIP_ITEM, struct.pack("<BB", BACKPACK, slot))
            self.client.pump(1.5)
        return made

    def undress(self):
        """Destroys every equipped item, so that the next set starts from no piece (and the check can run again)."""
        for slot in range(EQUIPMENT_SLOTS):
            self.client.send(CMSG_DESTROYITEM, struct.pack("<BBBBBB", BACKPACK, slot, 0, 0, 0, 0))
        self.client.pump(1.5)
        clear_backpack(self.realm, self.client)

    def check_bonuses(self, label, bonuses, worn):
        """Each bonus up to `worn` pieces is on the character, each one above is not."""
        have = self.auras()
        for threshold, spells in sorted(bonuses.items()):
            on = threshold <= worn
            for spell in spells:
                self.results.check("%s, %d pieces: the %d-piece bonus %d is %s" % (
                    label, worn, threshold, spell, "on" if on else "off"), (spell in have) == on, "auras %s" % sorted(have))
