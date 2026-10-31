"""The dungeon loot side of the check kit (spec jvbastel/WOWAI#240): one loot client and the helpers that the
dungeon loot checks share (wowai/test-realm/checks/t242-t247).

    from kit import Realm, Results
    from loot_kit import LootClient, forced_drop, kill_and_loot, loaded_drops, spec_drops
    realm = Realm()
    c = realm.login("PLAYER242", "Ragefirerog", race=2, cls=4, client_class=LootClient)
    drops = spec_drops("ragefire_chasm")         # {(loot entry, item): chance} of the spec's confirmed drops
    with forced_drop(realm, c, 11517, 71000):     # the Forever row at 100%, restored on exit
        loot = kill_and_loot(realm, c, 11517)     # the item IDs of the loot window, or None
    c.query_item(14145)                           # (quality, (min, max) damage), "missing" or None
    c.quest_rewards(5724)                         # ([fixed reward items], [reward choice items]) or None

The spec rows are read with the generator's own reader (forever/generator/forever_gen), so a check never keeps a
copy of the spec chances.
"""
import contextlib
import struct
import sys
from pathlib import Path

from kit import spawn
import wowclient

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "forever" / "generator"))
from forever_gen import loot, spec, stock_world  # noqa: E402  (the spec reader and the stock loot entries)

CMSG_ITEM_QUERY_SINGLE, SMSG_ITEM_QUERY_SINGLE_RESPONSE = 86, 88
CMSG_QUEST_QUERY, SMSG_QUEST_QUERY_RESPONSE = 92, 93
CMSG_LOOT, CMSG_LOOT_RELEASE, SMSG_LOOT_RESPONSE = 349, 351, 352
NOT_FOUND = 0x80000000
CREATURE_LOOT = "forever_creature_loot_template"
RARE, UNCOMMON = 3, 2


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
        """([fixed reward items], [reward choice items]) of the quest query answer (the quest log), or None."""
        self.quest_answers.pop(quest, None)
        self.send(CMSG_QUEST_QUERY, struct.pack("<I", quest))
        self.pump(wait)
        body = self.quest_answers.get(quest)
        if body is None:
            return None
        # 15 uint32 (ID ... quest flags; build 5875 has RewMoneyMaxLevel), 4 x (reward item, count), 6 x (choice, count).
        values = struct.unpack_from("<20I", body, 4 * 15)
        return [item for item in values[0:8:2] if item], [item for item in values[8::2] if item]

    def quest_choices(self, quest, wait=1.5):
        """The reward choice items of the quest query answer, or None."""
        rewards = self.quest_rewards(quest, wait)
        return None if rewards is None else rewards[1]


def kill_and_loot(realm, client, entry):
    """Spawns the boss, kills it (the client's blow, so it may loot) and opens its loot: the item IDs, or None. The
    spawn row goes."""
    guid = spawn(realm, client, entry)
    try:
        client.gm(".die", select=guid, wait=2.0)
        return client.open_loot(guid)
    finally:
        client.gm(".npc delete", select=guid, wait=1.0)


@contextlib.contextmanager
def forced_drop(realm, client, entry, item=None, reference=None, chance=100):
    """The Forever row of `item` (and, with `reference`, the row of that reference) in the creature loot entry `entry`
    at `chance` for the body of the `with`. The rows get back the chance they had on exit, also after an error."""
    where = []
    if item is not None:
        where.append("item = %d" % item)
    if reference is not None:
        where.append("mincountOrRef = -%d" % reference)
    rows = "entry = %d AND (%s)" % (entry, " OR ".join(where))
    saved = [line.split("\t") for line in realm.sql(
        "mangos", "SELECT item, mincountOrRef, ChanceOrQuestChance FROM %s WHERE %s" % (CREATURE_LOOT, rows)).splitlines()
        if line]
    realm.sql("mangos", "UPDATE %s SET ChanceOrQuestChance = %s WHERE %s" % (CREATURE_LOOT, chance, rows))
    client.gm(".reload creature_loot_template", wait=3)
    try:
        yield
    finally:
        for saved_item, count, saved_chance in saved:
            realm.sql("mangos", "UPDATE %s SET ChanceOrQuestChance = %s WHERE entry = %d AND item = %s AND "
                      "mincountOrRef = %s" % (CREATURE_LOOT, saved_chance, entry, saved_item, count))
        client.gm(".reload creature_loot_template", wait=3)


def spec_rows(slug):
    """The drop and reward rows of forever/spec/loot_<slug>.tsv: {"loot_drops": [...], "loot_rewards": [...]}."""
    path = ROOT / "forever" / "spec" / ("loot_%s.tsv" % slug)
    return loot.read_loot_rows(str(path), spec.data_lines(path.read_text(encoding="utf-8")))


def spec_drops(slug):
    """{(creature loot entry, item): chance} of the confirmed drop rows of forever/spec/loot_<slug>.tsv."""
    creatures = stock_world.StockWorld.read().creatures
    return {(creatures[d.boss].loot_id, d.item): d.chance for d in spec_rows(slug)["loot_drops"]
            if d.status == loot.CONFIRMED}


def loaded_drops(realm, items):
    """{(loot entry, item): (chance rounded to 2 decimals, group, patch_min)} of the rows of `items` in the Forever
    creature loot table."""
    loaded = {}
    for line in realm.sql("mangos", "SELECT entry, item, ChanceOrQuestChance, groupid, patch_min FROM %s WHERE item IN "
                          "(%s)" % (CREATURE_LOOT, ", ".join(str(i) for i in sorted(items)))).splitlines():
        if line:
            entry, item, chance, group, patch_min = line.split("\t")
            loaded[(int(entry), int(item))] = (round(float(chance), 2), int(group), int(patch_min))
    return loaded


def spec_drops_loaded(realm, drops):
    """True, and the loaded rows: the Forever creature loot table holds each drop of `drops` ({(loot entry, item):
    chance}) at its chance, outside the stock groups (group 0), from patch 0."""
    loaded = loaded_drops(realm, {item for _, item in drops})
    return loaded == {key: (round(chance, 2), 0, 0) for key, chance in drops.items()}, loaded
