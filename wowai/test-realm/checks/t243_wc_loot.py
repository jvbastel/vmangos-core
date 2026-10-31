#!/usr/bin/env python3
"""In-game check of jvbastel/WOWAI#243, the Forever loot of Wailing Caverns (spec #240), on the check kit. Each step
is one run on the same slot; an acquire between them restarts the slot with the other rules and keeps the database
(same lease), as in t242_rfc_loot.py:

    wowai/test-realm/realm.sh --ticket N acquire --forever
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t243_wc_loot.py forever
    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t243_wc_loot.py vanilla

An Orc Rogue spawns Lady Anacondra (3671) where it stands, kills her with the GM `.die` (its own killing blow, so
it may loot) and opens her loot window. It asks the server for item and quest data as the client does for a tooltip
and the quest log (CMSG_ITEM_QUERY_SINGLE, CMSG_QUEST_QUERY).

forever  (rules on) forever_creature_loot_template holds each drop row of forever/spec/loot_wailing_caverns.tsv.
         Lady Anacondra, with the Forever row of Snake Eye Kaleidoscope (71101) forced to 100%, drops it and one
         item of her stock equal-chance group (5404, 6446, 10412), which has its Forever quality. Tail Spike (6448)
         has its Forever quality and damage. Quest 914 offers Hammerbone (71106) next to its two stock choices;
         quest 962 offers Heat Resistant Mitts (71104) and Safety Boots (71105).
vanilla  (rules off) Tail Spike has its stock quality and damage, Snake Eye Kaleidoscope is no item, quests 914 and
         962 offer their stock choices, and Lady Anacondra, with the forced row still in
         forever_creature_loot_template, drops no Kaleidoscope.
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
ANACONDRA = 3671
KALEIDOSCOPE, SPEC_CHANCE = 71101, 2.04
# The stock equal-chance group of Lady Anacondra (reference 30126) with the Forever quality of each item.
ANACONDRA_DROPS = {5404: 3, 6446: 2, 10412: 3}
TAIL_SPIKE = 6448
LEADERS, LEADERS_STOCK, HAMMERBONE = 914, [6505, 6504], 71106
SERPENTBLOOM, MITTS, BOOTS = 962, 71104, 71105
RARE, UNCOMMON = 3, 2
# The spec drops (forever/spec/loot_wailing_caverns.tsv): (boss loot entry, item): chance.
DROPS = {(3653, 71100): 1.17, (3671, 71101): 2.04, (3670, 71102): 1.71, (3674, 71103): 2.63}


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



def force_kaleidoscope(chance=100):
    """The Forever row of Snake Eye Kaleidoscope at `chance` (100: Lady Anacondra drops it on each kill)."""
    realm.sql("mangos", "UPDATE %s SET ChanceOrQuestChance = %s WHERE entry = %d AND item = %d"
              % (CREATURE_LOOT, chance, ANACONDRA, KALEIDOSCOPE))
    c.gm(".reload creature_loot_template", wait=3)


mode = sys.argv[1] if len(sys.argv) > 1 else ""
if mode not in ("forever", "vanilla"):
    sys.exit("usage: t243_wc_loot.py forever|vanilla")

realm = Realm()
results = Results()
realm.mark("t243 %s" % mode)
c = realm.login("PLAYER243", "Wailingrog", race=2, cls=4, client_class=LootClient)

if mode == "forever":
    loaded = {}
    for line in realm.sql("mangos", "SELECT entry, item, ChanceOrQuestChance, groupid, patch_min FROM %s WHERE item IN (%s)"
                          % (CREATURE_LOOT, ", ".join(str(i) for _, i in DROPS))).splitlines():
        entry, item, chance, group, patch_min = line.split("\t")
        loaded[(int(entry), int(item))] = (round(float(chance), 2), int(group), int(patch_min))
    results.check("%s holds each WC drop row of the spec, outside the stock groups, from patch 0" % CREATURE_LOOT,
                  loaded == {key: (chance, 0, 0) for key, chance in DROPS.items()}, "got %r" % loaded)
    clear_backpack(realm, c)
    force_kaleidoscope()
    try:
        loot = kill_and_loot(ANACONDRA)
    finally:
        force_kaleidoscope(SPEC_CHANCE)
    results.check("Lady Anacondra drops the new Snake Eye Kaleidoscope (%d)" % KALEIDOSCOPE,
                  loot and KALEIDOSCOPE in loot, "loot %r" % loot)
    stock = [item for item in (loot or []) if item in ANACONDRA_DROPS]
    results.check("Lady Anacondra drops one item of her stock equal-chance group", len(stock) == 1, "loot %r" % loot)
    answer = c.query_item(stock[0]) if stock else None
    results.check("that stock boss drop has its Forever quality",
                  answer not in (None, "missing") and answer[0] == ANACONDRA_DROPS[stock[0]], "answer %r" % (answer,))
    answer = c.query_item(KALEIDOSCOPE)
    results.check("Snake Eye Kaleidoscope is an item (rare)", answer not in (None, "missing") and answer[0] == RARE,
                  "answer %r" % (answer,))
    answer = c.query_item(TAIL_SPIKE)
    results.check("Tail Spike has its Forever quality and damage (rare, 17-33)", answer == (RARE, (17, 33)),
                  "answer %r" % (answer,))
    choices = c.quest_choices(LEADERS)
    results.check("quest %d offers Hammerbone (%d)" % (LEADERS, HAMMERBONE), choices == LEADERS_STOCK + [HAMMERBONE],
                  "choices %r" % choices)
    choices = c.quest_choices(SERPENTBLOOM)
    results.check("quest %d offers Heat Resistant Mitts and Safety Boots" % SERPENTBLOOM, choices == [MITTS, BOOTS],
                  "choices %r" % choices)
else:
    answer = c.query_item(TAIL_SPIKE)
    results.check("Tail Spike has its stock quality and damage (uncommon, 14-26)", answer == (UNCOMMON, (14, 26)),
                  "answer %r" % (answer,))
    answer = c.query_item(KALEIDOSCOPE)
    results.check("Snake Eye Kaleidoscope is not loaded", answer == "missing", "answer %r" % (answer,))
    choices = c.quest_choices(LEADERS)
    results.check("quest %d offers its two stock choices" % LEADERS, choices == LEADERS_STOCK, "choices %r" % choices)
    choices = c.quest_choices(SERPENTBLOOM)
    results.check("quest %d offers no choice (stock)" % SERPENTBLOOM, choices == [], "choices %r" % choices)
    force_kaleidoscope()
    try:
        loot = kill_and_loot(ANACONDRA)
        results.check("Lady Anacondra drops no Kaleidoscope, even at 100%% in %s" % CREATURE_LOOT,
                      loot is not None and KALEIDOSCOPE not in loot, "loot %r" % loot)
    finally:
        force_kaleidoscope(SPEC_CHANCE)

results.finish()
