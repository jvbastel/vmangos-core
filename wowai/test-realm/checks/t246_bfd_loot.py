#!/usr/bin/env python3
"""In-game check of jvbastel/WOWAI#246, the Forever loot of Blackfathom Deeps (spec #240), on the check kit (batched
test of spec #240). Each step is one run on the same slot; an acquire between them restarts the slot with the other
rules and keeps the database (same lease):

    wowai/test-realm/realm.sh --ticket N acquire --forever
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t246_bfd_loot.py forever
    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t246_bfd_loot.py vanilla

A Tauren Warrior spawns Ghamoo-ra (4887) where it stands, kills it with the GM `.die` (the killing blow is its own, so
it may loot) and opens its loot window. It asks the server for item and quest data as the client does for a tooltip
and the quest log (CMSG_ITEM_QUERY_SINGLE, CMSG_QUEST_QUERY). The client class is copied from t242_rfc_loot.py.

forever  (rules on) forever_creature_loot_template holds each drop row of forever/spec/loot_blackfathom_deeps.tsv.
         Ghamoo-ra, with the Forever row of Spiked Shell Band (71400) forced to 100%, drops it, and drops one of its
         two stock items (reference 30156, one equal-chance group): Tortoise Armor (6907) or Ghamoo-ra's Bind (6908),
         both rare in Forever (Ghamoo-ra's Bind is uncommon in stock). Strike of the Hydra (6909) keeps its stock
         damage, Darkwater Talwar (11121) has its Forever damage 26-49. Quest 6561 Blackfathom Villainy (Horde)
         offers Dark Ritual Leggings (71408) and Cultist's Armguards (71409) next to its two stock choices.
vanilla  (rules off) Darkwater Talwar has its stock quality and damage (uncommon, 20-39), Spiked Shell Band is no item
         and quest 6561 offers its two stock choices. Ghamoo-ra, with the forced row still in
         forever_creature_loot_template, drops no Spiked Shell Band.
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
GHAMOORA = 4887
SHELL_BAND = 71400
GHAMOORA_DROPS = (6907, 6908)
DARKWATER_TALWAR, STRIKE_OF_THE_HYDRA = 11121, 6909
VILLAINY, VILLAINY_STOCK, VILLAINY_NEW = 6561, [7001, 7002], [71408, 71409]
RARE, UNCOMMON = 3, 2
# The spec drops (forever/spec/loot_blackfathom_deeps.tsv): (boss loot entry, item): chance.
GELIHAST, LORGUS, KELRIS = 6243, 12902, 4832
DROPS = {(GHAMOORA, SHELL_BAND): 4.19, (GELIHAST, 71401): 4.33, (LORGUS, 71402): 14.0, (LORGUS, 71403): 9.9,
         (LORGUS, 71404): 9.96, (KELRIS, 71405): 3.15}
SPEC_CHANCE = DROPS[(GHAMOORA, SHELL_BAND)]


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
    """Spawns the boss, kills it (the warrior's blow) and opens its loot: the item IDs, or None. The spawn row goes."""
    guid = spawn(realm, c, entry)
    try:
        c.gm(".die", select=guid, wait=2.0)
        return c.open_loot(guid)
    finally:
        c.gm(".npc delete", select=guid, wait=1.0)


def set_band_chance(chance):
    realm.sql("mangos", "UPDATE %s SET ChanceOrQuestChance = %s WHERE entry = %d AND item = %d"
              % (CREATURE_LOOT, chance, GHAMOORA, SHELL_BAND))
    c.gm(".reload creature_loot_template", wait=3)


mode = sys.argv[1] if len(sys.argv) > 1 else ""
if mode not in ("forever", "vanilla"):
    sys.exit("usage: t246_bfd_loot.py forever|vanilla")

realm = Realm()
results = Results()
realm.mark("t246 %s" % mode)
c = realm.login("PLAYER246", "Bfdwarrior", race=6, cls=1, client_class=LootClient)

if mode == "forever":
    loaded = {}
    for line in realm.sql("mangos", "SELECT entry, item, ChanceOrQuestChance, groupid, patch_min FROM %s WHERE item IN (%s)"
                          % (CREATURE_LOOT, ", ".join(str(i) for _, i in DROPS))).splitlines():
        entry, item, chance, group, patch_min = line.split("\t")
        loaded[(int(entry), int(item))] = (round(float(chance), 2), int(group), int(patch_min))
    results.check("%s holds each BFD drop row of the spec, outside the stock groups, from patch 0" % CREATURE_LOOT,
                  loaded == {key: (chance, 0, 0) for key, chance in DROPS.items()}, "got %r" % loaded)
    clear_backpack(realm, c)
    set_band_chance(100)
    try:
        loot = kill_and_loot(GHAMOORA)
    finally:
        set_band_chance(SPEC_CHANCE)
    results.check("Ghamoo-ra drops the new Spiked Shell Band (%d)" % SHELL_BAND, loot and SHELL_BAND in loot,
                  "loot %r" % loot)
    stock = [item for item in (loot or []) if item in GHAMOORA_DROPS]
    results.check("Ghamoo-ra drops one item of its stock equal-chance group", len(stock) == 1, "loot %r" % loot)
    answer = c.query_item(stock[0]) if stock else None
    results.check("that stock boss drop is rare (Forever)", answer not in (None, "missing") and answer[0] == RARE,
                  "answer %r" % (answer,))
    answer = c.query_item(SHELL_BAND)
    results.check("Spiked Shell Band is a rare item", answer not in (None, "missing") and answer[0] == RARE,
                  "answer %r" % (answer,))
    answer = c.query_item(DARKWATER_TALWAR)
    results.check("Darkwater Talwar has its Forever quality and damage (rare, 26-49)", answer == (RARE, (26, 49)),
                  "answer %r" % (answer,))
    answer = c.query_item(STRIKE_OF_THE_HYDRA)
    results.check("Strike of the Hydra keeps its damage (rare, 67-102)", answer == (RARE, (67, 102)),
                  "answer %r" % (answer,))
    choices = c.quest_choices(VILLAINY)
    results.check("quest %d offers Dark Ritual Leggings and Cultist's Armguards" % VILLAINY,
                  choices == VILLAINY_STOCK + VILLAINY_NEW, "choices %r" % choices)
else:
    answer = c.query_item(DARKWATER_TALWAR)
    results.check("Darkwater Talwar has its stock quality and damage (uncommon, 20-39)", answer == (UNCOMMON, (20, 39)),
                  "answer %r" % (answer,))
    answer = c.query_item(SHELL_BAND)
    results.check("Spiked Shell Band is not loaded (the server does not know the item)", answer == "missing",
                  "answer %r" % (answer,))
    choices = c.quest_choices(VILLAINY)
    results.check("quest %d offers its two stock choices" % VILLAINY, choices == VILLAINY_STOCK, "choices %r" % choices)
    set_band_chance(100)
    try:
        loot = kill_and_loot(GHAMOORA)
    finally:
        set_band_chance(SPEC_CHANCE)
    results.check("Ghamoo-ra drops no Spiked Shell Band, even at 100%% in %s" % CREATURE_LOOT,
                  loot is not None and SHELL_BAND not in loot, "loot %r" % loot)

results.finish()
