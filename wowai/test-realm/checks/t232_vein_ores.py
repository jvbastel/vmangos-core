#!/usr/bin/env python3
"""In-game check of jvbastel/WOWAI#232, the Forever ores in the mining veins (spec #225), on the check kit.

    # A slot with the Forever rules:
    wowai/test-realm/realm.sh --ticket N acquire --forever
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t232_vein_ores.py forever
    # A slot without them (the table of the migration is there, maybe filled by an earlier Forever lease):
    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t232_vein_ores.py vanilla

An Orc Hunter with Mining 300 and a Mining Pick mines Copper Veins (gameobject 1731, loot ID 1502) that a GM
spawns where it stands. Pyrite has a 1% rate, so the check forces it through the loot data: it sets the Forever
row of Pyrite in loot ID 1502 to 100% (its own roll, group 0) and reloads gameobject_loot_template. At the end it
puts the row back as it was and reloads again.

forever: forever_gameobject_loot_template holds every drop row of forever/spec/mining.tsv with its chance and
         group, and the mined Copper Vein gives Pyrite (with Copper Ore).
vanilla: the same forced row is in forever_gameobject_loot_template, but the core does not read the table: three
         mined Copper Veins give Copper Ore and never Pyrite.
"""
import struct
import sys
from pathlib import Path

from kit import Realm, Results, clear_backpack
from wowclient import CMSG_CAST_SPELL, SpellClient, pack_guid

ROOT = Path(__file__).resolve().parents[3]
MINING_SPEC = ROOT / "forever" / "spec" / "mining.tsv"
TABLE = "forever_gameobject_loot_template"
MINING_SKILL, APPRENTICE_MINING, MINING_PICK = 186, 2575, 2901
COPPER_VEIN, COPPER_LOOT, COPPER_ORE = 1731, 1502, 2770
HIGHGUID_GAMEOBJECT = 0xF110
TARGET_FLAG_GAMEOBJECT = 0x800
CMSG_LOOT_RELEASE, SMSG_LOOT_RESPONSE = 351, 352
TRIES = {"forever": 1, "vanilla": 3}


def spec_rows():
    lines = [line for line in MINING_SPEC.read_text(encoding="utf-8").splitlines() if line and not line.startswith("#")]
    header = lines[0].split("\t")
    return [dict(zip(header, line.split("\t"))) for line in lines[1:]]


class LootClient(SpellClient):
    """A SpellClient that keeps the SMSG_LOOT_RESPONSE bodies: (looted guid, [item IDs])."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.loot = []

    def _observe(self, opcode, body):
        super()._observe(opcode, body)
        if opcode == SMSG_LOOT_RESPONSE and len(body) >= 9:
            # uint64 guid, uint8 loot type (0: an error byte follows), uint32 gold, uint8 count, then per item
            # uint8 slot, uint32 item, uint32 count, uint32 display, uint32 0, uint32 random property, uint8 type.
            guid, kind = struct.unpack_from("<QB", body, 0)
            items = []
            if kind and len(body) >= 14:
                count = body[13]
                items = [struct.unpack_from("<I", body, 14 + 22 * k + 1)[0] for k in range(count)]
            self.loot.append((guid, items))

    def mine(self, guid, wait=10):
        """Casts Mining at the gameobject and waits for its loot window: the item IDs, or None."""
        start = len(self.loot)
        self.send(CMSG_CAST_SPELL, struct.pack("<IH", APPRENTICE_MINING, TARGET_FLAG_GAMEOBJECT) + pack_guid(guid))
        for _ in range(int(wait * 2)):
            self.pump(0.5)
            if len(self.loot) > start:
                break
        found = [items for looted, items in self.loot[start:] if looted == guid]
        self.send(CMSG_LOOT_RELEASE, struct.pack("<Q", guid))
        self.pump(0.5)
        return found[0] if found else None


mode = sys.argv[1] if len(sys.argv) > 1 else ""
if mode not in TRIES:
    sys.exit("usage: t232_vein_ores.py forever|vanilla")

rows = spec_rows()
pyrite = int(next(row["item"] for row in rows if row["kind"] == "ore" and row["name"] == "Pyrite"))
drops = {(int(row["lootId"]), int(row["item"])): (float(row["chance"]), int(row["groupid"]))
         for row in rows if row["kind"] == "drop"}

realm = Realm()
results = Results()

if mode == "forever":
    ores = sorted({item for _, item in drops})
    loaded = {}
    for line in realm.sql("mangos", "SELECT entry, item, ChanceOrQuestChance, groupid FROM %s WHERE item IN (%s)"
                          % (TABLE, ", ".join(map(str, ores)))).splitlines():
        entry, item, chance, group = line.split("\t")
        loaded[(int(entry), int(item))] = (round(float(chance), 2), int(group))
    results.check("%s holds every drop row of mining.tsv" % TABLE, loaded == drops, "got %r" % loaded)
    stock = realm.sql("mangos", "SELECT COUNT(*) FROM gameobject_loot_template WHERE entry = %d" % COPPER_LOOT).strip()
    copied = realm.sql("mangos", "SELECT COUNT(*) FROM %s WHERE entry = %d AND item <> %d"
                       % (TABLE, COPPER_LOOT, pyrite)).strip()
    results.check("the stock rows of loot ID %d are copied" % COPPER_LOOT, stock == copied and stock != "0",
                  "stock %s, copied %s" % (stock, copied))

# Force Pyrite: its row in loot ID 1502 at 100%, group 0. Keep the row as it was, to put it back.
before = realm.sql("mangos", "SELECT ChanceOrQuestChance, groupid FROM %s WHERE entry = %d AND item = %d"
                   % (TABLE, COPPER_LOOT, pyrite)).strip()
realm.sql("mangos", "INSERT INTO %s (entry, item, ChanceOrQuestChance, groupid) VALUES (%d, %d, 100, 0) "
          "ON DUPLICATE KEY UPDATE ChanceOrQuestChance = 100, groupid = 0" % (TABLE, COPPER_LOOT, pyrite))

name = "Pyriteon" if mode == "forever" else "Pyriteoff"
c = realm.login("PLAYER232", name, race=2, cls=3, client_class=LootClient)
me = c.guid
spawned = []
try:
    c.gm(".reload gameobject_loot_template", wait=3)
    clear_backpack(realm, c)
    c.gm(".learn %d" % APPRENTICE_MINING, select=me)
    c.gm(".setskill %d 300 300" % MINING_SKILL, select=me)
    c.gm(".additem %d" % MINING_PICK, select=me)
    c.pump(1)
    mined = []
    for _ in range(TRIES[mode]):
        c.gm(".gobject add %d" % COPPER_VEIN, wait=2)
        low = int(realm.sql("mangos", "SELECT MAX(guid) FROM gameobject WHERE id = %d" % COPPER_VEIN).split()[-1])
        spawned.append(low)
        mined.append(c.mine((HIGHGUID_GAMEOBJECT << 48) | (COPPER_VEIN << 24) | low))
    results.check("each Copper Vein opens and shows its loot", all(items is not None for items in mined),
                  "loot %r, cast results %r" % (mined, c.cast_results[-5:]))
    results.check("the Copper Veins give Copper Ore", all(items and COPPER_ORE in items for items in mined),
                  "loot %r" % mined)
    if mode == "forever":
        results.check("with the rules on, a Copper Vein gives Pyrite (%d)" % pyrite,
                      all(items and pyrite in items for items in mined), "loot %r" % mined)
    else:
        results.check("with the rules off, no Copper Vein gives Pyrite (%d), even at 100%% in %s" % (pyrite, TABLE),
                      all(items is not None and pyrite not in items for items in mined), "loot %r" % mined)
finally:
    for low in spawned:
        c.gm(".gobject delete %d" % low, wait=1)
    if before:
        chance, group = before.split("\t")
        realm.sql("mangos", "UPDATE %s SET ChanceOrQuestChance = %s, groupid = %s WHERE entry = %d AND item = %d"
                  % (TABLE, chance, group, COPPER_LOOT, pyrite))
    else:
        realm.sql("mangos", "DELETE FROM %s WHERE entry = %d AND item = %d" % (TABLE, COPPER_LOOT, pyrite))
    c.gm(".reload gameobject_loot_template", wait=3)

results.finish()
