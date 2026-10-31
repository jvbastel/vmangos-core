#!/usr/bin/env python3
"""In-game check of jvbastel/WOWAI#247, the Forever loot of the Stockade (spec #240), on the check kit. Each step is
one run on the same slot; an acquire between them restarts the slot with the other rules and keeps the database
(same lease), as in t242_rfc_loot.py:

    wowai/test-realm/realm.sh --ticket N acquire --forever
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t247_stockade_loot.py forever
    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t247_stockade_loot.py vanilla

A Human Rogue spawns Kam Deepfury (1666) where it stands, kills him with the GM `.die` (the killing blow is its own,
so it may loot) and opens his loot window. It asks the server for item and quest data as the client does for a
tooltip and the quest log (CMSG_ITEM_QUERY_SINGLE, CMSG_QUEST_QUERY). The client class is a copy of the one in
t242_rfc_loot.py (a check script runs on its own and cannot import another check).

Forced drop: in forever_creature_loot_template, the Forever row of Demolition Girdle (71503) and Kam's copied row of
his stock reference 30098 (whose one equal-chance item is Kam's Walking Stick, 2280) are set to 100%, then restored.

forever  (rules on) forever_creature_loot_template holds each drop row of forever/spec/loot_stockade.tsv. Kam
         Deepfury drops his stock Kam's Walking Stick, which is rare with its Forever damage (39-60), and the new
         Demolition Girdle (rare). Quest 377 offers Town Clerk's Mittens (71516) next to its two stock choices.
vanilla  (rules off) Kam's Walking Stick has its stock quality and damage (uncommon, 41-62), Demolition Girdle is no
         item, and quest 377 offers its two stock choices. Kam, with the forced rows still in
         forever_creature_loot_template, drops no Demolition Girdle.
"""
import struct
import sys

from kit import Realm, Results, clear_backpack, spawn
import wowclient

CMSG_ITEM_QUERY_SINGLE, SMSG_ITEM_QUERY_SINGLE_RESPONSE = 86, 88
CMSG_QUEST_QUERY, SMSG_QUEST_QUERY_RESPONSE = 92, 93
CMSG_LOOT, CMSG_LOOT_RELEASE, SMSG_LOOT_RESPONSE = 349, 351, 352
NOT_FOUND = 0x80000000
CREATURE_LOOT = "forever_creature_loot_template"
KAM, KAM_REFERENCE = 1666, 30098
WALKING_STICK, GIRDLE, MITTENS = 2280, 71503, 71516
GIRDLE_CHANCE = 2.19
CRIME, CRIME_STOCK = 377, [2033, 2906]
RARE, UNCOMMON = 3, 2
# The spec drops (forever/spec/loot_stockade.tsv): (boss loot entry, item): chance.
DROPS = {
    (1696, 71500): 1.5, (1696, 71501): 1.7, (1696, 71502): 1.68,
    (1666, 71503): 2.19, (1666, 71504): 1.83,
    (1717, 71505): 1.75, (1717, 71506): 1.47, (1717, 71507): 1.79,
    (1663, 71508): 2.0, (1663, 71509): 1.76, (1663, 71510): 1.36,
    (1716, 71511): 0.98, (1716, 71512): 0.92, (1716, 71513): 1.23, (1716, 71514): 0.94,
}


class LootClient(wowclient.SpellClient):
    """A SpellClient that keeps the loot windows, item query answers and quest query answers."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.loot, self.item_answers, self.quest_answers = [], {}, {}

    def _observe(self, opcode, body):
        super()._observe(opcode, body)
        if opcode == SMSG_LOOT_RESPONSE and len(body) >= 9:
            # uint64 guid, uint8 loot type (0: an error byte follows), uint32 gold, uint8 count, then per item
            # uint8 slot, uint32 item, uint32 count, uint32 display, uint32 0, uint32 random property, uint8 type.
            guid, kind = struct.unpack_from("<QB", body, 0)
            items = []
            if kind and len(body) >= 14:
                items = [struct.unpack_from("<I", body, 14 + 22 * k + 1)[0] for k in range(body[13])]
            self.loot.append((guid, items))
        elif opcode == SMSG_ITEM_QUERY_SINGLE_RESPONSE and len(body) >= 4:
            self.item_answers[struct.unpack_from("<I", body, 0)[0]] = body
        elif opcode == SMSG_QUEST_QUERY_RESPONSE and len(body) >= 4:
            self.quest_answers[struct.unpack_from("<I", body, 0)[0]] = body

    def open_loot(self, guid, wait=5):
        """The item IDs of the loot window of a dead unit, or None."""
        start = len(self.loot)
        self.send(CMSG_LOOT, struct.pack("<Q", guid))
        for _ in range(int(wait * 2)):
            self.pump(0.5)
            if len(self.loot) > start:
                break
        found = [items for looted, items in self.loot[start:] if looted == guid]
        self.send(CMSG_LOOT_RELEASE, struct.pack("<Q", guid))
        self.pump(0.5)
        return found[0] if found else None

    def query_item(self, entry, wait=1.5):
        """(quality, (min, max) damage) of the item query answer, "missing" for a not-found answer, or None."""
        self.item_answers.pop(entry, None)
        self.item_answers.pop(entry | NOT_FOUND, None)
        self.send(CMSG_ITEM_QUERY_SINGLE, struct.pack("<IQ", entry, 0))
        self.pump(wait)
        if entry | NOT_FOUND in self.item_answers:
            return "missing"
        body = self.item_answers.get(entry)
        if body is None:
            return None
        pos = 12  # entry, class, subclass; then four names, then 20 uint32 (quality at 1), 10 stats, 5 damages
        for _ in range(4):
            pos = body.index(b"\0", pos) + 1
        quality = struct.unpack_from("<I", body, pos + 4)[0]
        damage = struct.unpack_from("<ff", body, pos + 80 + 80)
        return quality, tuple(int(round(d)) for d in damage)

    def quest_choices(self, quest, wait=1.5):
        """The reward choice items of the quest query answer (the quest log), or None."""
        self.quest_answers.pop(quest, None)
        self.send(CMSG_QUEST_QUERY, struct.pack("<I", quest))
        self.pump(wait)
        body = self.quest_answers.get(quest)
        if body is None:
            return None
        # 15 uint32 (ID ... quest flags; build 5875 has RewMoneyMaxLevel), 4 x (reward item, count), 6 x (choice, count).
        values = struct.unpack_from("<12I", body, 4 * (15 + 8))
        return [item for item in values[::2] if item]


def kill_and_loot(entry):
    """Spawns the boss, kills it (the rogue's blow) and opens its loot: the item IDs, or None. The spawn row goes."""
    guid = spawn(realm, c, entry)
    try:
        c.gm(".die", select=guid, wait=2.0)
        return c.open_loot(guid)
    finally:
        c.gm(".npc delete", select=guid, wait=1.0)


def reference_chance():
    """The chance of Kam's copied row of his stock reference in the Forever table (as text), or ""."""
    return realm.sql("mangos", "SELECT ChanceOrQuestChance FROM %s WHERE entry = %d AND mincountOrRef = -%d"
                     % (CREATURE_LOOT, KAM, KAM_REFERENCE)).strip()


def force():
    """Kam's Demolition Girdle row and his reference row at 100%: each kill drops both items."""
    realm.sql("mangos", "UPDATE %s SET ChanceOrQuestChance = 100 WHERE entry = %d AND (item = %d OR mincountOrRef = -%d)"
              % (CREATURE_LOOT, KAM, GIRDLE, KAM_REFERENCE))
    c.gm(".reload creature_loot_template", wait=3)


def restore(reference):
    realm.sql("mangos", "UPDATE %s SET ChanceOrQuestChance = %s WHERE entry = %d AND item = %d"
              % (CREATURE_LOOT, GIRDLE_CHANCE, KAM, GIRDLE))
    if reference:
        realm.sql("mangos", "UPDATE %s SET ChanceOrQuestChance = %s WHERE entry = %d AND mincountOrRef = -%d"
                  % (CREATURE_LOOT, reference, KAM, KAM_REFERENCE))
    c.gm(".reload creature_loot_template", wait=3)


mode = sys.argv[1] if len(sys.argv) > 1 else ""
if mode not in ("forever", "vanilla"):
    sys.exit("usage: t247_stockade_loot.py forever|vanilla")

realm = Realm()
results = Results()
realm.mark("t247 %s" % mode)
c = realm.login("PLAYER247", "Stockaderog", race=1, cls=4, client_class=LootClient)
reference = reference_chance()
results.check("%s holds Kam's copied row of his stock reference %d" % (CREATURE_LOOT, KAM_REFERENCE), reference != "",
              "no row")

if mode == "forever":
    loaded = {}
    for line in realm.sql("mangos", "SELECT entry, item, ChanceOrQuestChance, groupid, patch_min FROM %s WHERE item IN (%s)"
                          % (CREATURE_LOOT, ", ".join(str(i) for _, i in DROPS))).splitlines():
        entry, item, chance, group, patch_min = line.split("\t")
        loaded[(int(entry), int(item))] = (round(float(chance), 2), int(group), int(patch_min))
    results.check("%s holds each drop row of the spec, outside the stock groups, from patch 0" % CREATURE_LOOT,
                  loaded == {key: (chance, 0, 0) for key, chance in DROPS.items()}, "got %r" % loaded)
    clear_backpack(realm, c)
    force()
    try:
        loot = kill_and_loot(KAM)
    finally:
        restore(reference)
    results.check("Kam Deepfury drops his stock Kam's Walking Stick (%d)" % WALKING_STICK, loot and WALKING_STICK in loot,
                  "loot %r" % loot)
    results.check("Kam Deepfury drops the new Demolition Girdle (%d)" % GIRDLE, loot and GIRDLE in loot, "loot %r" % loot)
    answer = c.query_item(WALKING_STICK)
    results.check("Kam's Walking Stick has its Forever quality and damage (rare, 39-60)", answer == (RARE, (39, 60)),
                  "answer %r" % (answer,))
    answer = c.query_item(GIRDLE)
    results.check("Demolition Girdle is an item (rare)", answer not in (None, "missing") and answer[0] == RARE,
                  "answer %r" % (answer,))
    choices = c.quest_choices(CRIME)
    results.check("quest %d offers Town Clerk's Mittens (%d)" % (CRIME, MITTENS), choices == CRIME_STOCK + [MITTENS],
                  "choices %r" % choices)
else:
    answer = c.query_item(WALKING_STICK)
    results.check("Kam's Walking Stick has its stock quality and damage (uncommon, 41-62)", answer == (UNCOMMON, (41, 62)),
                  "answer %r" % (answer,))
    answer = c.query_item(GIRDLE)
    results.check("Demolition Girdle is not loaded (the server does not know the item)", answer == "missing",
                  "answer %r" % (answer,))
    choices = c.quest_choices(CRIME)
    results.check("quest %d offers its two stock choices" % CRIME, choices == CRIME_STOCK, "choices %r" % choices)
    force()
    try:
        loot = kill_and_loot(KAM)
    finally:
        restore(reference)
    results.check("Kam drops no Demolition Girdle, even at 100%% in %s" % CREATURE_LOOT,
                  loot is not None and GIRDLE not in loot, "loot %r" % loot)

results.finish()
