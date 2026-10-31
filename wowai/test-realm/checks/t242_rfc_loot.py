#!/usr/bin/env python3
"""In-game check of jvbastel/WOWAI#242, the Forever loot of Ragefire Chasm (spec #240), on the check kit. Each step
is one run on the same slot; an acquire between them restarts the slot with the other rules and keeps the database
(same lease), as in t233_switch_cycle.py:

    wowai/test-realm/realm.sh --ticket N acquire --forever
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t242_rfc_loot.py forever
    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t242_rfc_loot.py vanilla
    wowai/test-realm/realm.sh --ticket N acquire --forever
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t242_rfc_loot.py forever-again

An Orc Rogue (not a Warrior) spawns Ragefire Chasm bosses where it stands, kills them with the GM `.die` (the
killing blow is its own, so it may loot) and opens their loot window. It asks the server for item and quest data
as the client does for a tooltip and the quest log (CMSG_ITEM_QUERY_SINGLE, CMSG_QUEST_QUERY).

forever        (rules on) forever_creature_loot_template holds each drop row of forever/spec/loot_ragefire_chasm.tsv.
               Oggleflint (11517), with the Forever row of Trogg Scepter (71000) forced to 100%, drops it.
               Taragaman (11520) drops one of its three stock items (its stock equal-chance group), and that item
               has its Forever quality (rare). Cursed Felblade (14145) has its Forever damage. Quest 5724 offers
               Garrison Cuffs (71006) next to its two stock choices. The rogue gets a Trogg Scepter to keep.
vanilla        (rules off) Cursed Felblade has its stock stats, Trogg Scepter is no item (the server answers "not
               found" and keeps the rogue's row without loading it), and quest 5724 offers its two stock choices.
               Oggleflint, with the forced row still in forever_creature_loot_template, drops no Trogg Scepter.
forever-again  (rules on) The Trogg Scepter is an item again and the rogue's row is still there.
"""
import struct
import sys

from kit import Realm, Results, clear_backpack, spawn
import wowclient

CMSG_ITEM_QUERY_SINGLE, SMSG_ITEM_QUERY_SINGLE_RESPONSE = 86, 88
CMSG_QUEST_QUERY, SMSG_QUEST_QUERY_RESPONSE = 92, 93
CMSG_LOOT, CMSG_LOOT_RELEASE, SMSG_LOOT_RESPONSE = 349, 351, 352
NOT_FOUND = 0x80000000
CREATURE_LOOT, QUEST_TABLE = "forever_creature_loot_template", "forever_quest_template"
OGGLEFLINT, TARAGAMAN = 11517, 11520
TROGG_SCEPTER, GARRISON_CUFFS, CURSED_FELBLADE = 71000, 71006, 14145
TARAGAMAN_DROPS = (14145, 14148, 14149)
SATCHEL, SATCHEL_STOCK = 5724, [15452, 15453]
SPEC_CHANCE = 3.22
RARE, UNCOMMON = 3, 2
# The spec drops (forever/spec/loot_ragefire_chasm.tsv): (boss loot entry, item): chance.
DROPS = {(11517, 71000): 3.22, (11517, 71001): 2.65, (11517, 71002): 2.93,
         (11519, 71003): 2.84, (11519, 71004): 2.89, (11519, 71005): 2.82}
KEPT_ITEM = r"\[Forever\] Item %d exists only under the Forever rules" % TROGG_SCEPTER


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


def scepter_rows():
    c.gm(".save", select=c.guid)
    c.pump(1)
    return realm.sql("characters", "SELECT COUNT(*) FROM character_inventory WHERE guid = %d AND item_id = %d"
                     % (c.guid, TROGG_SCEPTER)).strip()


def force_scepter():
    """The Forever row of Trogg Scepter at 100% (the spec chance is 3.22%): Oggleflint drops it on each kill."""
    realm.sql("mangos", "UPDATE %s SET ChanceOrQuestChance = 100 WHERE entry = %d AND item = %d"
              % (CREATURE_LOOT, OGGLEFLINT, TROGG_SCEPTER))
    c.gm(".reload creature_loot_template", wait=3)


def restore_scepter():
    realm.sql("mangos", "UPDATE %s SET ChanceOrQuestChance = %s WHERE entry = %d AND item = %d"
              % (CREATURE_LOOT, SPEC_CHANCE, OGGLEFLINT, TROGG_SCEPTER))
    c.gm(".reload creature_loot_template", wait=3)


mode = sys.argv[1] if len(sys.argv) > 1 else ""
if mode not in ("forever", "vanilla", "forever-again"):
    sys.exit("usage: t242_rfc_loot.py forever|vanilla|forever-again")

realm = Realm()
results = Results()
realm.mark("t242 %s" % mode)
c = realm.login("PLAYER242", "Ragefirerog", race=2, cls=4, client_class=LootClient)

if mode == "forever":
    loaded = {}
    for line in realm.sql("mangos", "SELECT entry, item, ChanceOrQuestChance, groupid, patch_min FROM %s WHERE item IN (%s)"
                          % (CREATURE_LOOT, ", ".join(str(i) for _, i in DROPS))).splitlines():
        entry, item, chance, group, patch_min = line.split("\t")
        loaded[(int(entry), int(item))] = (round(float(chance), 2), int(group), int(patch_min))
    results.check("%s holds each drop row of the spec, outside the stock groups, from patch 0" % CREATURE_LOOT,
                  loaded == {key: (chance, 0, 0) for key, chance in DROPS.items()}, "got %r" % loaded)
    clear_backpack(realm, c)
    force_scepter()
    try:
        loot = kill_and_loot(OGGLEFLINT)
        results.check("Oggleflint drops the new Trogg Scepter (%d)" % TROGG_SCEPTER, loot and TROGG_SCEPTER in loot,
                      "loot %r" % loot)
    finally:
        restore_scepter()
    loot = kill_and_loot(TARAGAMAN)
    stock = [item for item in (loot or []) if item in TARAGAMAN_DROPS]
    results.check("Taragaman drops one item of its stock equal-chance group", len(stock) == 1, "loot %r" % loot)
    answer = c.query_item(stock[0]) if stock else None
    results.check("that stock boss drop is rare (Forever)", answer not in (None, "missing") and answer[0] == RARE,
                  "answer %r" % (answer,))
    answer = c.query_item(CURSED_FELBLADE)
    results.check("Cursed Felblade has its Forever quality and damage (rare, 19-37)", answer == (RARE, (19, 37)),
                  "answer %r" % (answer,))
    choices = c.quest_choices(SATCHEL)
    results.check("quest %d offers Garrison Cuffs (%d)" % (SATCHEL, GARRISON_CUFFS),
                  choices == SATCHEL_STOCK + [GARRISON_CUFFS], "choices %r" % choices)
    c.gm(".additem %d" % TROGG_SCEPTER, select=c.guid)
    results.check("the rogue keeps a Trogg Scepter", scepter_rows() == "1", "rows %s" % scepter_rows())
elif mode == "vanilla":
    answer = c.query_item(CURSED_FELBLADE)
    results.check("Cursed Felblade has its stock quality and damage (uncommon, 16-31)", answer == (UNCOMMON, (16, 31)),
                  "answer %r" % (answer,))
    answer = c.query_item(TROGG_SCEPTER)
    results.check("Trogg Scepter is not loaded (the server does not know the item)", answer == "missing",
                  "answer %r" % (answer,))
    results.check("the server keeps the rogue's Trogg Scepter without loading it", realm.wait_log(KEPT_ITEM, 5, c))
    results.check("the rogue's Trogg Scepter row stays through a save", scepter_rows() == "1", "rows %s" % scepter_rows())
    choices = c.quest_choices(SATCHEL)
    results.check("quest %d offers its two stock choices" % SATCHEL, choices == SATCHEL_STOCK, "choices %r" % choices)
    force_scepter()
    try:
        loot = kill_and_loot(OGGLEFLINT)
        results.check("Oggleflint drops no Trogg Scepter, even at 100%% in %s" % CREATURE_LOOT,
                      loot is not None and TROGG_SCEPTER not in loot, "loot %r" % loot)
    finally:
        restore_scepter()
else:
    answer = c.query_item(TROGG_SCEPTER)
    results.check("Trogg Scepter is an item again (rare)", answer not in (None, "missing") and answer[0] == RARE,
                  "answer %r" % (answer,))
    results.check("no Forever item is kept unloaded", not realm.wait_log(KEPT_ITEM, 2, c))
    results.check("the rogue's Trogg Scepter row is still there", scepter_rows() == "1", "rows %s" % scepter_rows())

results.finish()
