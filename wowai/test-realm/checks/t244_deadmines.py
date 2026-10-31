#!/usr/bin/env python3
"""In-game check of jvbastel/WOWAI#244, the Forever loot of the Deadmines (spec #240), on the check kit. Each step is
one run on the same slot; an acquire between them restarts the slot with the other rules and keeps the database
(same lease), as in t242_rfc_loot.py:

    wowai/test-realm/realm.sh --ticket N acquire --forever
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t244_deadmines.py forever
    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t244_deadmines.py vanilla

A Human Rogue spawns Sneed (643) where it stands, kills him with the GM `.die` (the killing blow is its own, so it
may loot) and opens his loot window. It asks the server for item and quest data as the client does for a tooltip
and the quest log (CMSG_ITEM_QUERY_SINGLE, CMSG_QUEST_QUERY).

forever  (rules on) forever_creature_loot_template holds each drop row of forever/spec/loot_deadmines.tsv. Sneed,
         with the Forever row of Bandsaw Wristbands (71201) forced to 100%, drops it and one item of his stock
         equal-chance group (Taskmaster Axe 5194 or Gold-flecked Gloves 5195), which has its Forever values: Gold-
         flecked Gloves is rare (stock uncommon), Taskmaster Axe has 31-48 damage (stock 42-64). Quest 167 offers
         Miner's Revenge (1893), Miner's Workgloves (71208) and Miner's Workboots (71209) as choices, with no fixed
         reward; quest 2040 offers Bravo's Armbands (71210) and Dreamer's Leggings (71211) next to its stock choices.
vanilla  (rules off) Gold-flecked Gloves is uncommon, Taskmaster Axe has 42-64 damage, Bandsaw Wristbands is no item,
         and quest 167 gives Miner's Revenge as a fixed reward with no choice.
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
SNEED = 643
BANDSAW_WRISTBANDS, SPEC_CHANCE = 71201, 0.95
TASKMASTER_AXE, GOLD_FLECKED_GLOVES = 5194, 5195
SNEED_DROPS = (TASKMASTER_AXE, GOLD_FLECKED_GLOVES)
OH_BROTHER, MINERS_REVENGE = 167, 1893
ASSAULT, ASSAULT_STOCK = 2040, [7606, 7607]
RARE, UNCOMMON = 3, 2
# The spec drops (forever/spec/loot_deadmines.tsv): (boss loot entry, item): chance.
DROPS = {(644, 71200): 1.56, (643, 71201): 0.95, (1763, 71202): 1.3, (645, 71203): 1.04, (646, 71204): 1.01,
         (642, 71205): 1.82}


class LootClient(wowclient.SpellClient):
    """A SpellClient that keeps the loot windows, item query answers and quest query answers (t242_rfc_loot.py)."""

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

    def quest_rewards(self, quest, wait=1.5):
        """(fixed reward items, reward choice items) of the quest query answer (the quest log), or None."""
        self.quest_answers.pop(quest, None)
        self.send(CMSG_QUEST_QUERY, struct.pack("<I", quest))
        self.pump(wait)
        body = self.quest_answers.get(quest)
        if body is None:
            return None
        # 15 uint32 (ID ... quest flags; build 5875 has RewMoneyMaxLevel), 4 x (reward item, count), 6 x (choice, count).
        values = struct.unpack_from("<20I", body, 4 * 15)
        return [i for i in values[0:8:2] if i], [i for i in values[8::2] if i]


def kill_and_loot(entry):
    """Spawns the boss, kills it (the rogue's blow) and opens its loot: the item IDs, or None. The spawn row goes."""
    guid = spawn(realm, c, entry)
    try:
        c.gm(".die", select=guid, wait=2.0)
        return c.open_loot(guid)
    finally:
        c.gm(".npc delete", select=guid, wait=1.0)


def set_wristbands_chance(chance):
    realm.sql("mangos", "UPDATE %s SET ChanceOrQuestChance = %s WHERE entry = %d AND item = %d"
              % (CREATURE_LOOT, chance, SNEED, BANDSAW_WRISTBANDS))
    c.gm(".reload creature_loot_template", wait=3)


def check_stock_drop(item, gloves, axe):
    """Checks the stock boss drop `item` against the expected (quality) of the gloves and (damage) of the axe."""
    answer = c.query_item(item)
    if item == GOLD_FLECKED_GLOVES:
        results.check("Gold-flecked Gloves has quality %d" % gloves, answer not in (None, "missing") and answer[0] == gloves,
                      "answer %r" % (answer,))
    else:
        results.check("Taskmaster Axe has %d-%d damage" % axe, answer not in (None, "missing") and answer[1] == axe,
                      "answer %r" % (answer,))


mode = sys.argv[1] if len(sys.argv) > 1 else ""
if mode not in ("forever", "vanilla"):
    sys.exit("usage: t244_deadmines.py forever|vanilla")

realm = Realm()
results = Results()
realm.mark("t244 %s" % mode)
c = realm.login("PLAYER244", "Deadminerog", race=1, cls=4, client_class=LootClient)

if mode == "forever":
    loaded = {}
    for line in realm.sql("mangos", "SELECT entry, item, ChanceOrQuestChance, groupid, patch_min FROM %s WHERE item IN (%s)"
                          % (CREATURE_LOOT, ", ".join(str(i) for _, i in DROPS))).splitlines():
        entry, item, chance, group, patch_min = line.split("\t")
        loaded[(int(entry), int(item))] = (round(float(chance), 2), int(group), int(patch_min))
    results.check("%s holds each Deadmines drop row of the spec, outside the stock groups, from patch 0" % CREATURE_LOOT,
                  loaded == {key: (chance, 0, 0) for key, chance in DROPS.items()}, "got %r" % loaded)
    clear_backpack(realm, c)
    set_wristbands_chance(100)
    try:
        loot = kill_and_loot(SNEED)
    finally:
        set_wristbands_chance(SPEC_CHANCE)
    results.check("Sneed drops the new Bandsaw Wristbands (%d)" % BANDSAW_WRISTBANDS,
                  loot and BANDSAW_WRISTBANDS in loot, "loot %r" % loot)
    stock = [item for item in (loot or []) if item in SNEED_DROPS]
    results.check("Sneed drops one item of his stock equal-chance group", len(stock) == 1, "loot %r" % loot)
    # Both stock drops, whichever one fell.
    check_stock_drop(GOLD_FLECKED_GLOVES, RARE, (31, 48))
    check_stock_drop(TASKMASTER_AXE, RARE, (31, 48))
    answer = c.query_item(BANDSAW_WRISTBANDS)
    results.check("Bandsaw Wristbands is rare", answer not in (None, "missing") and answer[0] == RARE,
                  "answer %r" % (answer,))
    rewards = c.quest_rewards(OH_BROTHER)
    results.check("quest %d offers 1893, 71208 and 71209 as choices with no fixed reward" % OH_BROTHER,
                  rewards == ([], [MINERS_REVENGE, 71208, 71209]), "rewards %r" % (rewards,))
    rewards = c.quest_rewards(ASSAULT)
    results.check("quest %d offers Bravo's Armbands and Dreamer's Leggings" % ASSAULT,
                  rewards == ([], ASSAULT_STOCK + [71210, 71211]), "rewards %r" % (rewards,))
else:
    check_stock_drop(GOLD_FLECKED_GLOVES, UNCOMMON, (42, 64))
    check_stock_drop(TASKMASTER_AXE, UNCOMMON, (42, 64))
    answer = c.query_item(BANDSAW_WRISTBANDS)
    results.check("Bandsaw Wristbands is not loaded (the server does not know the item)", answer == "missing",
                  "answer %r" % (answer,))
    rewards = c.quest_rewards(OH_BROTHER)
    results.check("quest %d gives Miner's Revenge as its fixed reward, with no choice" % OH_BROTHER,
                  rewards == ([MINERS_REVENGE], []), "rewards %r" % (rewards,))

results.finish()
