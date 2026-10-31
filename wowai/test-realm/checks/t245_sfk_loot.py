#!/usr/bin/env python3
"""In-game check of jvbastel/WOWAI#245, the Forever loot of Shadowfang Keep (spec #240), on the check kit. Each step
is one run on the same slot; an acquire between them restarts the slot with the other rules and keeps the database
(same lease), as in t242_rfc_loot.py:

    wowai/test-realm/realm.sh --ticket N acquire --forever
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t245_sfk_loot.py forever
    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t245_sfk_loot.py vanilla

An Undead Rogue spawns Rethilgore where it stands, kills him with the GM `.die` (the killing blow is its own, so it
may loot) and opens his loot window. It asks the server for item and quest data as the client does for a tooltip
and the quest log (CMSG_ITEM_QUERY_SINGLE, CMSG_QUEST_QUERY). The loot client is the one of t242_rfc_loot.py, with
the fixed rewards of the quest answer too.

forever  (rules on) forever_creature_loot_template holds each drop row of forever/spec/loot_shadowfang_keep.tsv.
         Rethilgore (3914), with the Forever row of Cell Keeper's Claws (71300) forced to 100%, drops it and his
         stock Rugged Spaulders (5254, a stock row at 100%), which is rare in Forever (common in stock). Butcher's
         Cleaver (1292) has its Forever damage. Quest 1098 offers Ghostly Mantle and the two new shoulders as choices
         and has no fixed reward; quest 1013 offers Tattered Mittens (71309) next to its two stock choices.
vanilla  (rules off) Rugged Spaulders is common and Butcher's Cleaver has its stock damage, Cell Keeper's Claws is
         no item, quest 1098 gives Ghostly Mantle as its fixed reward with no choices, and Rethilgore, with the forced
         row still in forever_creature_loot_template, drops no Cell Keeper's Claws.
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
RETHILGORE = 3914
CLAWS, RUGGED_SPAULDERS, BUTCHERS_CLEAVER = 71300, 5254, 1292
SPEC_CHANCE = 1.68
DEATHSTALKERS, GHOSTLY_MANTLE, DEATHSTALKERS_NEW = 1098, 3324, [71307, 71308]
BOOK_OF_UR, BOOK_OF_UR_STOCK, MITTENS = 1013, [6335, 4534], 71309
RARE, UNCOMMON, COMMON = 3, 2, 1
# The spec drops (forever/spec/loot_shadowfang_keep.tsv): (boss loot entry, item): chance.
DROPS = {(3914, 71300): 1.68, (3914, 71301): 2.09, (3887, 71302): 1.72, (4278, 71303): 1.67,
         (4279, 71304): 1.72, (4274, 71305): 1.99, (3927, 71306): 1.75}


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
        return [item for item in values[:8:2] if item], [item for item in values[8::2] if item]


def kill_and_loot(entry):
    """Spawns the boss, kills it (the rogue's blow) and opens its loot: the item IDs, or None. The spawn row goes."""
    guid = spawn(realm, c, entry)
    try:
        c.gm(".die", select=guid, wait=2.0)
        return c.open_loot(guid)
    finally:
        c.gm(".npc delete", select=guid, wait=1.0)


def set_claws_chance(chance):
    realm.sql("mangos", "UPDATE %s SET ChanceOrQuestChance = %s WHERE entry = %d AND item = %d"
              % (CREATURE_LOOT, chance, RETHILGORE, CLAWS))
    c.gm(".reload creature_loot_template", wait=3)


def forced_rethilgore_loot():
    """Rethilgore's loot with the Forever row of Cell Keeper's Claws at 100% (the spec chance is 1.68%)."""
    set_claws_chance(100)
    try:
        return kill_and_loot(RETHILGORE)
    finally:
        set_claws_chance(SPEC_CHANCE)


mode = sys.argv[1] if len(sys.argv) > 1 else ""
if mode not in ("forever", "vanilla"):
    sys.exit("usage: t245_sfk_loot.py forever|vanilla")

realm = Realm()
results = Results()
realm.mark("t245 %s" % mode)
c = realm.login("PLAYER245", "Sfkrogue", race=5, cls=4, client_class=LootClient)

if mode == "forever":
    loaded = {}
    for line in realm.sql("mangos", "SELECT entry, item, ChanceOrQuestChance, groupid, patch_min FROM %s WHERE item IN (%s)"
                          % (CREATURE_LOOT, ", ".join(str(i) for _, i in DROPS))).splitlines():
        entry, item, chance, group, patch_min = line.split("\t")
        loaded[(int(entry), int(item))] = (round(float(chance), 2), int(group), int(patch_min))
    results.check("%s holds each SFK drop row of the spec, outside the stock groups, from patch 0" % CREATURE_LOOT,
                  loaded == {key: (chance, 0, 0) for key, chance in DROPS.items()}, "got %r" % loaded)
    clear_backpack(realm, c)
    loot = forced_rethilgore_loot()
    results.check("Rethilgore drops the new Cell Keeper's Claws (%d)" % CLAWS, loot and CLAWS in loot, "loot %r" % loot)
    results.check("Rethilgore drops his stock Rugged Spaulders (%d)" % RUGGED_SPAULDERS, loot and RUGGED_SPAULDERS in loot,
                  "loot %r" % loot)
    answer = c.query_item(RUGGED_SPAULDERS)
    results.check("Rugged Spaulders is rare (Forever)", answer not in (None, "missing") and answer[0] == RARE,
                  "answer %r" % (answer,))
    answer = c.query_item(CLAWS)
    results.check("Cell Keeper's Claws is a rare item with 25-48 damage", answer == (RARE, (25, 48)), "answer %r" % (answer,))
    answer = c.query_item(BUTCHERS_CLEAVER)
    results.check("Butcher's Cleaver has its Forever damage (rare, 19-36)", answer == (RARE, (19, 36)), "answer %r" % (answer,))
    rewards = c.quest_rewards(DEATHSTALKERS)
    results.check("quest %d offers Ghostly Mantle and the two new shoulders, with no fixed reward" % DEATHSTALKERS,
                  rewards == ([], [GHOSTLY_MANTLE] + DEATHSTALKERS_NEW), "rewards %r" % (rewards,))
    rewards = c.quest_rewards(BOOK_OF_UR)
    results.check("quest %d offers Tattered Mittens (%d)" % (BOOK_OF_UR, MITTENS),
                  rewards == ([], BOOK_OF_UR_STOCK + [MITTENS]), "rewards %r" % (rewards,))
else:
    answer = c.query_item(RUGGED_SPAULDERS)
    results.check("Rugged Spaulders is common (stock)", answer not in (None, "missing") and answer[0] == COMMON,
                  "answer %r" % (answer,))
    answer = c.query_item(BUTCHERS_CLEAVER)
    results.check("Butcher's Cleaver has its stock damage (rare, 23-32)", answer == (RARE, (23, 32)), "answer %r" % (answer,))
    answer = c.query_item(CLAWS)
    results.check("Cell Keeper's Claws is not loaded (the server does not know the item)", answer == "missing",
                  "answer %r" % (answer,))
    rewards = c.quest_rewards(DEATHSTALKERS)
    results.check("quest %d gives Ghostly Mantle as its fixed reward, with no choices" % DEATHSTALKERS,
                  rewards == ([GHOSTLY_MANTLE], []), "rewards %r" % (rewards,))
    rewards = c.quest_rewards(BOOK_OF_UR)
    results.check("quest %d offers its two stock choices" % BOOK_OF_UR, rewards == ([], BOOK_OF_UR_STOCK),
                  "rewards %r" % (rewards,))
    clear_backpack(realm, c)
    loot = forced_rethilgore_loot()
    results.check("Rethilgore drops no Cell Keeper's Claws, even at 100%% in %s" % CREATURE_LOOT,
                  loot is not None and CLAWS not in loot, "loot %r" % loot)

results.finish()
