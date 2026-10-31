"""The dungeon loot rows of the spec (spec jvbastel/WOWAI#240, #242): forever/spec/loot_<dungeon>*.tsv.

Two file kinds, told apart by their column line (forever/REFRESH.md, "Dungeon loot", has the layout):
- an items file (ITEM_COLUMNS): the stock boss drops and quest rewards with Forever stats (kind stock), new items
  with a stock twin (kind item) and new items with no stock twin (kind new). Each file names its item ID block.
- a loot file (LOOT_COLUMNS): new boss drops (kind drop), new or swapped quest reward choices (kind reward) and
  lifted class locks of quest rewards (kind unlock).

Every row names its source (a Wowhead URL or the client build) and the date. A row whose status is not confirmed
is left out and listed (loot-list.tsv), with the drops and rewards of its item. resolve() turns the confirmed item
rows into items.Item rows, so that items.py checks and writes them like every other item: a new item with no stock
twin copies the stock weapon or armor item of its class, subclass and slot nearest in item level (then of the same
quality, then the lowest ID) and takes that item's display unless the row names one. A new item, and a stock item in
the stock gear file, take the disenchant ID of the stock gear of the same class and quality nearest in item level
(then the lowest ID): a copied row of another level or quality would give the wrong materials.

A drop becomes a full copy of the stock loot entry (the boss's creature loot, or a reference that only this boss
uses) plus the new row, as mining.py does for the veins: the core reads a Forever loot entry instead of all stock
rows of that entry (Forever::Table). The new row is outside every stock equal-chance group (groupid 0) and loads
from patch DUNGEON_PATCH_MIN on. A quest with a reward row becomes a copy of every stock row of the quest with
the Forever reward choices. The generator reads the stock creatures, quests and references from stock_world.py.
"""
import re
from collections import namedtuple

from . import equip_stats, items as item_rows
from .common import NO_VALUE, SpecError, check_kind, fail_unless, number_text, rows_of

ITEM_COLUMNS = ("kind", "entry", "foreverItem", "sourceItem", "name", "quality", "itemLevel", "requiredLevel", "bonding",
                "class", "subclass", "inventoryType", "delay", "damage", "armor", "resistances", "stats", "bonuses",
                "equipSpells", "displayId", "sellPrice", "foreverStats", "leftOut", "status", "source", "date", "notes")
LOOT_COLUMNS = ("kind", "item", "name", "target", "targetName", "loot", "chance", "replaces", "count", "status",
                "source", "date", "notes")
STOCK, TWIN, NEW = "stock", "item", "new"
ITEM_KINDS = (STOCK, TWIN, NEW)
DROP, REWARD, UNLOCK = "drop", "reward", "unlock"
# The `replaces` value of a reward row whose item is a stock fixed reward that becomes a choice (#245).
FIXED = "fixed"
# The fixed reward slots of quest_template (RewItemId1-4).
FIXED_REWARD_SLOTS = 4
LOOT_KINDS = (DROP, REWARD, UNLOCK)
CONFIRMED = "confirmed"
STATUSES = (CONFIRMED, "unconfirmed", "unreadable")
CREATURE, REFERENCE = "creature", "reference"
CREATURE_TABLE, REFERENCE_TABLE, QUEST_TABLE = ("forever_creature_loot_template", "forever_reference_loot_template",
                                                "forever_quest_template")
TABLES = (CREATURE_TABLE, REFERENCE_TABLE, QUEST_TABLE)
# The six dungeons of spec #240 are open from the first content patch: a new drop loads from patch 0 to 10.
DUNGEON_PATCH_MIN, PATCH_MAX = 0, 10
QUEST_CHOICES = 6
# Every class (item_template allowable_class): a lifted class lock.
ALL_CLASSES = -1
SOURCE = re.compile(r"(https://nether\.wowhead\.com/tooltip/\w+/\d+\?dataEnv=16&locale=0|https://www\.wowhead\.com/forever/\S+"
                    r"|client \d+\.\d+\.\d+\.\d+)$")
DATE = re.compile(r"\d{4}-\d{2}-\d{2}$")
BLOCK = re.compile(r"^# Item ID block: (\d+)-(\d+)\.", re.M)
LIST_FILE = "loot-list.tsv"

# One row of an items file. raw: {column: value}; block: (low, high) of its file; where: the row for an error.
LootItem = namedtuple("LootItem", "kind entry name status source date left_out raw block path where")
# A new boss drop. loot: CREATURE or REFERENCE; reference: the reference ID or None.
Drop = namedtuple("Drop", "item name boss boss_name loot reference chance status source date where")
# A quest reward row (kind reward or unlock). replaces: the stock choice it replaces, FIXED (the item leaves the fixed
# rewards and is added as a choice) or None; count: the item count.
Reward = namedtuple("Reward", "kind item name quest title replaces count status source date where")
# One line of the loot list: list (left-out, unconfirmed, unreadable), kind, id, name, detail.
Listed = namedtuple("Listed", "list kind id name detail")


def _source(where, raw):
    if not SOURCE.match(raw["source"]):
        raise SpecError("%s has no source: '%s' is no Wowhead Forever URL and no 'client <build>'" % (where, raw["source"]))
    if not DATE.match(raw["date"]):
        raise SpecError("%s: the date '%s' is not YYYY-MM-DD" % (where, raw["date"]))
    if raw["status"] not in STATUSES:
        raise SpecError("%s: status '%s' is none of %s" % (where, raw["status"], ", ".join(STATUSES)))


def read_item_rows(path, lines):
    with open(path, encoding="utf-8") as handle:
        m = BLOCK.search(handle.read())
    if not m:
        raise SpecError("%s: no '# Item ID block: <low>-<high>.' line in the header" % path)
    block = (int(m.group(1)), int(m.group(2)))
    rows = []
    for where, raw in rows_of(path, lines, ITEM_COLUMNS):
        where = "loot item %s" % where
        check_kind(where, raw["kind"], ITEM_KINDS)
        _source(where, raw)
        rows.append(LootItem(kind=raw["kind"], entry=int(raw["entry"]), name=raw["name"], status=raw["status"],
                             source=raw["source"], date=raw["date"], left_out=raw["leftOut"], raw=raw, block=block,
                             path=path, where=where))
    return {"loot_items": rows}


def read_loot_rows(path, lines):
    drops, rewards = [], []
    for where, raw in rows_of(path, lines, LOOT_COLUMNS):
        kind = raw["kind"]
        where = "loot %s %s" % (kind, where)
        check_kind(where, kind, LOOT_KINDS)
        _source(where, raw)
        common = dict(item=int(raw["item"]), name=raw["name"], status=raw["status"], source=raw["source"],
                      date=raw["date"], where=where)
        if kind == DROP:
            loot, _, reference = raw["loot"].partition(" ")
            if loot not in (CREATURE, REFERENCE) or (loot == REFERENCE) != reference.isdigit():
                raise SpecError("%s: loot '%s' is not 'creature' or 'reference <ID>'" % (where, raw["loot"]))
            chance = float(raw["chance"])
            if not 0 < chance <= 100:
                raise SpecError("%s: chance %s is not above 0 and at most 100" % (where, raw["chance"]))
            drops.append(Drop(boss=int(raw["target"]), boss_name=raw["targetName"], loot=loot,
                              reference=int(reference) if reference else None, chance=chance, **common))
        else:
            replaces = None if raw["replaces"] == NO_VALUE else FIXED if raw["replaces"] == FIXED else int(raw["replaces"])
            count = 1 if raw["count"] == NO_VALUE else int(raw["count"])
            rewards.append(Reward(kind=kind, quest=int(raw["target"]), title=raw["targetName"], replaces=replaces,
                                  count=count, **common))
    return {"loot_drops": drops, "loot_rewards": rewards}


def _number(raw, column):
    return None if raw[column] == NO_VALUE else int(raw[column])


def _template(row, world):
    """The stock item that a new item with no stock twin copies: the same class, subclass and slot, nearest in item
    level, then of the same quality, then the lowest ID."""
    raw = row.raw
    key = (_number(raw, "class"), _number(raw, "subclass"), _number(raw, "inventoryType"))
    if None in key:
        raise SpecError("%s: a new item with no stock twin needs class, subclass and inventoryType" % row.where)
    quality = item_rows.QUALITIES.get(raw["quality"])
    candidates = [g for g in world.gear if (g.item_class, g.subclass, g.inventory_type) == key]
    if not candidates:
        raise SpecError("%s: no stock item of class %d, subclass %d and slot %d to copy" % ((row.where,) + key))
    return min(candidates, key=lambda g: (abs(g.item_level - int(raw["itemLevel"])), g.quality != quality, g.entry))


def _disenchant(item_class, quality, item_level, world):
    """The disenchant ID of the stock gear of this class and quality nearest in item level, or None."""
    candidates = [g for g in world.gear if g.item_class == item_class and g.quality == quality and g.disenchant_id]
    if not candidates:
        return None
    return min(candidates, key=lambda g: (abs(g.item_level - item_level), g.entry)).disenchant_id


def _item(row, world):
    """The items.Item of a confirmed item row."""
    raw, where = row.raw, row.where
    bonuses = equip_stats.parse(where, raw["bonuses"])
    spells = [] if raw["equipSpells"] == NO_VALUE else [int(s) for s in raw["equipSpells"].split(",")]
    if len(bonuses) + len(spells) > item_rows.MAX_ITEM_SPELLS:
        raise SpecError("%s: %d bonuses and equip spells, at most %d" % (where, len(bonuses) + len(spells),
                                                                        item_rows.MAX_ITEM_SPELLS))
    quality = item_rows.named(where, "quality", raw["quality"], item_rows.QUALITIES)
    if row.kind == NEW:
        template = _template(row, world)
        source, display, item_class = template.entry, template.display_id, template.item_class
        if raw["sourceItem"] != NO_VALUE:
            raise SpecError("%s: a new item with no stock twin has no sourceItem" % where)
    else:
        source, display = int(raw["sourceItem"]), None
        gear = [g for g in world.gear if g.entry == source]
        item_class = gear[0].item_class if gear else None
    if raw["displayId"] != NO_VALUE:
        display = int(raw["displayId"])
    if display is None:
        raise SpecError("%s: a %s row needs a displayId" % (where, row.kind))
    return item_rows.Item(
        kind=item_rows.STOCK if row.kind == STOCK else item_rows.NEW, entry=row.entry, source_item=source, name=row.name,
        quality=quality, item_level=int(raw["itemLevel"]),
        required_level=int(raw["requiredLevel"]), bonding=item_rows.named(where, "bonding", raw["bonding"], item_rows.BONDINGS),
        inventory_type=_number(raw, "inventoryType"), delay=_number(raw, "delay"),
        damage=item_rows.parse_damage(where, raw["damage"]), armor=int(raw["armor"]),
        resistances=item_rows.parse_resistances(where, raw["resistances"]), stats=item_rows.parse_stats(where, raw["stats"]),
        bonuses=bonuses, equip_spells=spells, display_id=display, sell_price=int(raw["sellPrice"]),
        forever_item=int(raw["foreverItem"]), forever_stats=raw["foreverStats"], notes=raw["notes"],
        disenchant_id=None if item_class is None else _disenchant(item_class, quality, int(raw["itemLevel"]), world))


def _blocks(spec):
    """Refuses a file with an item ID block outside the free item range or over the block of another file, and a
    new item outside the block of its file."""
    low, high = spec.item_id_range or (0, -1)
    blocks = {}
    for row in spec.loot_items:
        blocks[row.path] = row.block
        if row.kind != STOCK and not row.block[0] <= row.entry <= row.block[1]:
            raise SpecError("%s: entry %d is outside the item ID block %d-%d of its file" % ((row.where, row.entry) + row.block))
    for path, (first, last) in sorted(blocks.items()):
        if not low <= first <= last <= high:
            raise SpecError("%s: the item ID block %d-%d is outside the free item range %d-%d" % (path, first, last, low, high))
        for other, (o_first, o_last) in sorted(blocks.items()):
            if other != path and first <= o_last and o_first <= last:
                raise SpecError("%s: the item ID block %d-%d overlaps the block of %s" % (path, first, last, other))


def resolve(spec, stock):
    """(the spec with the confirmed loot items in spec.items, the class locks lifted and only the confirmed drops and
    rewards, the Listed rows). A drop or reward of a left-out item is left out too."""
    _blocks(spec)
    listed, left = [], {}
    new_items = []
    for row in spec.loot_items:
        if row.status != CONFIRMED:
            listed.append(Listed(row.status, "item", row.entry, row.name, row.source))
            left[row.entry] = row.status
            continue
        if row.kind != NEW and int(row.raw["sourceItem"]) not in stock.item_ids:
            raise SpecError("%s: sourceItem %s is no stock item" % (row.where, row.raw["sourceItem"]))
        new_items.append(_item(row, stock.world))
        if row.left_out != NO_VALUE:
            listed.append(Listed("left-out", "item", row.entry, row.name, row.left_out))
    drops, rewards = [], []
    for kind, rows, kept in (("drop", spec.loot_drops, drops), ("reward", spec.loot_rewards, rewards)):
        for row in rows:
            if row.status != CONFIRMED:
                listed.append(Listed(row.status, kind, row.item, row.name, row.source))
            elif row.item in left:
                listed.append(Listed(left[row.item], kind, row.item, row.name, "its item row is %s" % left[row.item]))
            else:
                kept.append(row)
    all_items = list(spec.items) + new_items
    for reward in rewards:
        if reward.kind == UNLOCK:
            found = [k for k, item in enumerate(all_items) if item.entry == reward.item]
            if not found:
                raise SpecError("%s: item %d has no item row to lift its class lock on" % (reward.where, reward.item))
            all_items[found[0]] = all_items[found[0]]._replace(allowable_class=ALL_CLASSES)
    spec = spec._replace(items=all_items, loot_drops=drops, loot_rewards=rewards)
    return spec, sorted(listed, key=lambda r: (r.list, r.kind, r.id))


def list_text(listed):
    """The loot list (loot-list.tsv): the left-out stats and the rows that are not confirmed."""
    lines = ["# The dungeon loot list (forever/generator, #242): left-out stats and effects, unconfirmed and unreadable rows.",
             "list\tkind\tid\tname\tdetail"]
    lines += ["%s\t%s\t%d\t%s\t%s" % row for row in listed]
    return "\n".join(lines) + "\n"


def _known_item(spec, stock, item):
    return item in {i.entry for i in spec.items} or item in stock.item_ids


def _loot_entry(drop, stock):
    """(table, stock table, loot entry) of a drop."""
    if drop.loot == REFERENCE:
        return REFERENCE_TABLE, "reference_loot_template", drop.reference
    return CREATURE_TABLE, "creature_loot_template", stock.world.creatures[drop.boss][0]


def check(spec, stock):
    """The rules of the drop and reward rows: a known boss with its stock name, a known item, a reference that only
    the boss uses, an item once per loot entry; a known quest with its stock title, a stock choice to replace, a free
    choice slot to add to, a reward item once per quest."""
    world = stock.world
    seen = set()
    for drop in spec.loot_drops:
        if drop.boss not in world.creatures:
            raise SpecError("%s: boss %d is no stock creature with loot (unknown boss)" % (drop.where, drop.boss))
        if world.creatures[drop.boss][1] != drop.boss_name:
            raise SpecError("%s: boss %d is '%s', not '%s'" % (drop.where, drop.boss, world.creatures[drop.boss][1], drop.boss_name))
        if not _known_item(spec, stock, drop.item):
            raise SpecError("%s: item %d is no item (no spec item and no stock item)" % (drop.where, drop.item))
        if drop.loot == REFERENCE:
            users = world.references.get(drop.reference)
            if users is None:
                raise SpecError("%s: reference %d is no stock reference loot entry" % (drop.where, drop.reference))
            if users != [world.creatures[drop.boss][0]]:
                raise SpecError("%s: reference %d is used by the creature loot %s, not by boss %d alone"
                                % (drop.where, drop.reference, ", ".join(map(str, users)) or "of no creature", drop.boss))
        key = _loot_entry(drop, stock)[::2] + (drop.item,)
        if key in seen:
            raise SpecError("%s: loot entry %d drops item %d twice" % (drop.where, key[1], drop.item))
        seen.add(key)
    for reward in spec.loot_rewards:
        where = reward.where
        if reward.quest not in world.quests:
            raise SpecError("%s: quest %d is no stock quest (unknown quest)" % (where, reward.quest))
        if world.quests[reward.quest][0] != reward.title:
            raise SpecError("%s: quest %d is '%s', not '%s'" % (where, reward.quest, world.quests[reward.quest][0], reward.title))
        if not _known_item(spec, stock, reward.item):
            raise SpecError("%s: item %d is no item (no spec item and no stock item)" % (where, reward.item))
        if reward.kind == UNLOCK and reward.item not in world.quests[reward.quest][1]:
            raise SpecError("%s: item %d is no stock reward choice of quest %d" % (where, reward.item, reward.quest))
    for quest in _reward_quests(spec):
        quest_choices(spec, stock, quest)


def _reward_quests(spec):
    """The quests with a reward row, in order."""
    return sorted({r.quest for r in spec.loot_rewards if r.kind == REWARD})


def quest_choices(spec, stock, quest):
    """{slot from 1: (item, count)} of the reward choices that the reward rows of `quest` change. Refuses a replaced
    item that is no stock choice, a quest with no free choice slot for an added item, and an item twice."""
    choices = list(stock.world.quests[quest][1])
    changed = {}
    for reward in sorted((r for r in spec.loot_rewards if r.kind == REWARD and r.quest == quest), key=lambda r: r.item):
        if reward.item in choices:
            raise SpecError("%s: item %d is a reward choice of quest %d already" % (reward.where, reward.item, quest))
        old = 0 if reward.replaces in (None, FIXED) else reward.replaces
        if old not in choices:
            raise SpecError("%s: %s" % (reward.where, "quest %d has no free reward choice slot" % quest if not old
                                        else "item %d is no stock reward choice of quest %d" % (old, quest)))
        slot = choices.index(old)
        choices[slot] = reward.item
        changed[slot + 1] = (reward.item, reward.count)
    return changed


def _quest_sql(spec, stock):
    lines = []
    for quest in _reward_quests(spec):
        title, stock_choices = stock.world.quests[quest]
        changed = quest_choices(spec, stock, quest)
        temp = "forever_quest_new"
        same = " AND ".join("`RewChoiceItemId%d`=%d" % (k, item) for k, item in enumerate(stock_choices, start=1))
        lines += [
            "",
            "-- Quest %d (%s): the reward choices %s." % (quest, title, ", ".join(
                "%d in slot %d" % (item, slot) for slot, (item, _) in sorted(changed.items()))),
            # Every patch row of the quest, so that the core still picks the row of the realm's patch.
            "CREATE TEMPORARY TABLE `%s` SELECT * FROM `quest_template` WHERE `entry`=%d;" % (temp, quest),
            # The stock rows must have the stock choices of the generator's data: with others, the SQL fails.
            fail_unless("(SELECT COUNT(*) FROM `%s`)>0 AND (SELECT COUNT(*) FROM `%s` WHERE NOT (%s))=0" % (temp, temp, same),
                        "forever_quest"),
            "UPDATE `%s` SET %s;" % (temp, ", ".join("`RewChoiceItemId%d`=%d, `RewChoiceItemCount%d`=%d" % (slot, item, slot, count)
                                                     for slot, (item, count) in sorted(changed.items()))),
            *_fixed_reward_sql(spec, quest, temp),
            "INSERT INTO `%s` SELECT * FROM `%s`;" % (QUEST_TABLE, temp),
            "DROP TEMPORARY TABLE `%s`;" % temp,
        ]
    return lines


def _fixed_reward_sql(spec, quest, temp):
    """SQL that takes each item of a `replaces` FIXED reward row of `quest` out of the fixed rewards of the quest copy
    `temp` (#245: Forever offers a stock fixed reward as one of the choices). The generator's stock data has no fixed
    rewards, so the SQL fails unless every stock row gives the item as a fixed reward. The count is set before the ID:
    MySQL applies the assignments of an UPDATE from left to right."""
    lines = []
    for item in sorted(r.item for r in spec.loot_rewards if r.kind == REWARD and r.quest == quest and r.replaces == FIXED):
        slots = range(1, FIXED_REWARD_SLOTS + 1)
        lines += [
            fail_unless("(SELECT COUNT(*) FROM `%s` WHERE NOT (%s))=0" % (
                temp, " OR ".join("`RewItemId%d`=%d" % (k, item) for k in slots)), "forever_fixed"),
            "UPDATE `%s` SET %s;" % (temp, ", ".join(
                "`RewItemCount{k}`=IF(`RewItemId{k}`={i}, 0, `RewItemCount{k}`), "
                "`RewItemId{k}`=IF(`RewItemId{k}`={i}, 0, `RewItemId{k}`)".format(k=k, i=item) for k in slots)),
        ]
    return lines


def _drop_sql(spec, stock):
    by_entry = {}
    for drop in spec.loot_drops:
        by_entry.setdefault(_loot_entry(drop, stock), []).append(drop)
    lines = []
    for (table, stock_table, entry) in sorted(by_entry):
        drops = sorted(by_entry[(table, stock_table, entry)], key=lambda d: d.item)
        bosses = sorted({d.boss for d in drops})
        lines += ["", "-- %s %d (%s): the stock rows and %s." % (
            "Reference loot" if table == REFERENCE_TABLE else "Creature loot", entry,
            ", ".join(d.boss_name for d in drops[:1]), ", ".join(d.name for d in drops))]
        # The boss must use this loot entry: with another one, the SQL fails (the generator has no world DB).
        for boss in bosses:
            loot_id = stock.world.creatures[boss][0]
            lines.append(fail_unless("(SELECT COUNT(*) FROM `creature_template` WHERE `entry`=%d)>0 AND (SELECT COUNT(*) "
                                     "FROM `creature_template` WHERE `entry`=%d AND `loot_id`<>%d)=0" % (boss, boss, loot_id),
                                     "forever_boss"))
            if table == REFERENCE_TABLE:
                lines.append(fail_unless("(SELECT COUNT(DISTINCT `entry`) FROM `creature_loot_template` WHERE "
                                         "`mincountOrRef`=-%d)=1 AND (SELECT COUNT(*) FROM `creature_loot_template` WHERE "
                                         "`mincountOrRef`=-%d AND `entry`=%d)>0" % (entry, entry, loot_id), "forever_reference"))
        lines.append("INSERT INTO `%s` SELECT * FROM `%s` WHERE `entry`=%d;" % (table, stock_table, entry))
        lines += ["INSERT INTO `%s` (`entry`, `item`, `ChanceOrQuestChance`, `groupid`, `mincountOrRef`, `maxcount`, "
                  "`patch_min`, `patch_max`) VALUES (%d, %d, %s, 0, 1, 1, %d, %d);"
                  % (table, entry, d.item, number_text(d.chance), DUNGEON_PATCH_MIN, PATCH_MAX) for d in drops]
    return lines


def server_sql(spec, stock):
    """The Forever loot and quest rows. They are copied with SELECT *, so they come before the removal marker column
    (removals.py)."""
    return _drop_sql(spec, stock) + _quest_sql(spec, stock)
