"""The item rows of the spec (items.tsv): new items (#226) and changed stock items (#227).

The 1.12 client has no item table: it asks the server for each item. So an item is server data only: a row
of forever_item_template, which the core reads with Forever.Rules = 1 over the stock item_template rows by
entry. The generator writes it as a copy of the stock row of its source item with the spec values on top, so
that a new column of item_template cannot break it. A new item (kind item) is a copy of another stock item
(class, subclass, slot, material, durability, sheath) and takes an ID from the free item range. A changed
stock item (kind stock) is a copy of its own stock row, keeps its ID and keeps every column that the spec
does not set: its description, item set and its spells other than on equip.

Every stat other than the base stats is a bonus: a stock equip spell (equip_stats.py).
"""
from collections import namedtuple

from . import equip_stats
from .common import NO_VALUE, SpecError, check_kind, copy_item_sql, fail_unless, id_range, optional_int, rows_of, sql_text

# stats: [(item_template stat type, value)]; bonuses: [equip_stats.Bonus]; equip_spells: [spell ID];
# quality, bonding and inventory_type: the numbers. inventory_type, delay, damage (min, max) and resistances
# ({column: value}): None keeps the source value. allowable_class: the class mask, and disenchant_id: the disenchant
# loot ID; None keeps the source value (loot.py: a lifted class lock of a quest reward, and the disenchant ID of the
# item's class, quality and level). block: the shield block value; None keeps the source value (loot.py, #243).
# buy_price: None keeps the source value (loot.py: a new dungeon item costs 5 times its sell price, #240).
Item = namedtuple("Item", "kind entry source_item name quality item_level required_level bonding inventory_type "
                          "delay damage armor resistances stats bonuses equip_spells display_id sell_price "
                          "forever_item forever_stats notes allowable_class disenchant_id block buy_price",
                  defaults=(None, None, None, None))

COLUMNS = ("kind", "entry", "sourceItem", "name", "quality", "itemLevel", "requiredLevel", "bonding", "inventoryType",
           "delay", "damage", "armor", "resistances", "stats", "bonuses", "equipSpells", "displayId", "sellPrice",
           "foreverItem", "foreverStats", "notes")
NEW, STOCK = "item", "stock"
KINDS = (NEW, STOCK)
QUALITIES = {"poor": 0, "common": 1, "uncommon": 2, "rare": 3, "epic": 4}
BONDINGS = {"none": 0, "pickup": 1, "equip": 2, "use": 3}
# item_template inventory_type of the weapon and off-hand slots.
SLOTS = {"oneHand": 13, "mainHand": 21, "offHand": 22, "twoHand": 17, "shield": 14, "ranged": 15, "holdable": 23}
# The item_template stat types of the 1.12 base stats.
STAT_TYPES = {"Mana": 0, "Health": 1, "Agi": 3, "Str": 4, "Int": 5, "Spi": 6, "Sta": 7}
RESISTANCES = ("Holy", "Fire", "Nature", "Frost", "Shadow", "Arcane")
NO_RESISTANCES = "none"
MAX_STATS, MAX_ITEM_SPELLS = 10, 5
SPELL_TRIGGER_ON_EQUIP = 1
TABLE = "forever_item_template"
# The columns of one spell slot of item_template, with their empty values.
SPELL_SLOT = (("spellid", "0"), ("spelltrigger", "0"), ("spellcharges", "0"), ("spellppmrate", "0"),
              ("spellcooldown", "-1"), ("spellcategory", "0"), ("spellcategorycooldown", "-1"))


def parse_stats(where, text):
    if text == NO_VALUE:
        return []
    stats = []
    for part in text.split(","):
        name, _, value = part.strip().partition(" ")
        if name not in STAT_TYPES or not value.lstrip("-").isdigit():
            raise SpecError("%s: the stat '%s' is not '<%s> <value>'" % (where, part.strip(), "|".join(STAT_TYPES)))
        stats.append((STAT_TYPES[name], int(value)))
    if len(stats) > MAX_STATS:
        raise SpecError("%s: %d stats, at most %d" % (where, len(stats), MAX_STATS))
    return stats


def parse_resistances(where, text):
    if text == NO_VALUE:
        return None
    values = dict.fromkeys(RESISTANCES, 0)
    for part in [] if text == NO_RESISTANCES else text.split(","):
        name, _, value = part.strip().partition(" ")
        if name not in values or not value.isdigit():
            raise SpecError("%s: the resistance '%s' is not '<%s> <value>'" % (where, part.strip(), "|".join(RESISTANCES)))
        values[name] = int(value)
    return {"%s_res" % name.lower(): value for name, value in values.items()}


def parse_damage(where, text):
    if text == NO_VALUE:
        return None
    low, _, high = text.partition("-")
    if not (low.isdigit() and high.isdigit()):
        raise SpecError("%s: the damage '%s' is not '<min>-<max>'" % (where, text))
    return int(low), int(high)


def named(where, column, text, names):
    if text not in names:
        raise SpecError("%s: %s '%s' is none of %s" % (where, column, text, ", ".join(names)))
    return names[text]


def read_rows(path, lines):
    items = []
    for where, raw in rows_of(path, lines, COLUMNS):
        check_kind(where, raw["kind"], KINDS)
        bonuses = equip_stats.parse(where, raw["bonuses"])
        spells = [] if raw["equipSpells"] == NO_VALUE else [int(s) for s in raw["equipSpells"].split(",")]
        if len(bonuses) + len(spells) > MAX_ITEM_SPELLS:
            raise SpecError("%s: %d bonuses and equip spells, at most %d" % (where, len(bonuses) + len(spells), MAX_ITEM_SPELLS))
        items.append(Item(
            kind=raw["kind"], entry=int(raw["entry"]), source_item=int(raw["sourceItem"]), name=raw["name"],
            quality=named(where, "quality", raw["quality"], QUALITIES), item_level=int(raw["itemLevel"]),
            required_level=int(raw["requiredLevel"]), bonding=named(where, "bonding", raw["bonding"], BONDINGS),
            inventory_type=None if raw["inventoryType"] == NO_VALUE else named(where, "inventoryType", raw["inventoryType"], SLOTS),
            delay=optional_int(raw, "delay"), damage=parse_damage(where, raw["damage"]),
            armor=int(raw["armor"]), resistances=parse_resistances(where, raw["resistances"]),
            stats=parse_stats(where, raw["stats"]), bonuses=bonuses, equip_spells=spells,
            display_id=int(raw["displayId"]), sell_price=int(raw["sellPrice"]),
            forever_item=optional_int(raw, "foreverItem"),
            forever_stats=raw["foreverStats"], notes=raw["notes"]))
    return {"items": items}


def new_material(where, entry, source_item, name, quality, item_level, display_id, sell_price, forever_item, notes):
    """A new item with no slot, stats or spells, bound to nobody (an ore, mining.py): `quality` by name."""
    return Item(kind=NEW, entry=entry, source_item=source_item, name=name,
                quality=named(where, "quality", quality, QUALITIES), item_level=item_level, required_level=0,
                bonding=BONDINGS["none"], inventory_type=None, delay=None, damage=None, armor=0, resistances=None,
                stats=[], bonuses=[], equip_spells=[], display_id=display_id, sell_price=sell_price,
                forever_item=forever_item, forever_stats=NO_VALUE, notes=notes)


def on_equip_spells(item):
    """The spells that the item gives on equip: the spell of each bonus, then the other equip spells."""
    return [bonus.spell for bonus in item.bonuses] + item.equip_spells


def check(spec, stock):
    """The ID rules of the items: a new item in the free item range, a changed stock item its own source
    with a stock ID, no entry twice; stock displays and stock equip spells (the bonus table too)."""
    low, high = id_range(spec.item_id_range)
    stock_spells = stock.spell.by_id()
    displays = stock.item_display_info.by_id()
    equip_stats.check(stock)
    seen = set()
    for item in spec.items:
        where = "item %s: entry %d" % (item.name, item.entry)
        if item.entry in seen:
            raise SpecError("%s is in the spec twice" % where)
        seen.add(item.entry)
        if item.kind == NEW and not low <= item.entry <= high:
            raise SpecError("%s is outside the free item ID range %d-%d" % (where, low, high))
        if item.kind == STOCK and item.entry != item.source_item:
            raise SpecError("%s: a stock item is its own source, not item %d" % (where, item.source_item))
        if item.kind == STOCK and item.entry >= low:
            raise SpecError("%s: a stock item has a stock ID, below the free item ID range %d-%d" % (where, low, high))
        if item.display_id not in displays:
            raise SpecError("%s: the display %d is no stock ItemDisplayInfo.dbc ID" % (where, item.display_id))
        for spell in on_equip_spells(item):
            if spell not in stock_spells:
                raise SpecError("%s: the equip spell %d is no stock spell" % (where, spell))


def item_columns(item):
    """The item_template columns that the spec sets on the copy of the source row, in this order. Every stat
    slot is set, so that none of the source stats stays. A new item also gets every spell slot and an empty
    description, item set and random property; a changed stock item keeps those (server_sql places its
    on-equip spells)."""
    stats = item.stats + [(0, 0)] * (MAX_STATS - len(item.stats))
    columns = [("entry", str(item.entry)), ("patch", "0"), ("name", sql_text(item.name))]
    if item.kind == NEW:
        columns.append(("description", "''"))
    columns += [("display_id", str(item.display_id)), ("quality", str(item.quality)),
                ("sell_price", str(item.sell_price)), ("item_level", str(item.item_level)),
                ("required_level", str(item.required_level)), ("bonding", str(item.bonding)),
                ("armor", str(item.armor))]
    if item.kind == NEW:
        columns += [("set_id", "0"), ("random_property", "0")]
    if item.inventory_type is not None:
        columns.append(("inventory_type", str(item.inventory_type)))
    if item.allowable_class is not None:
        columns.append(("allowable_class", str(item.allowable_class)))
    if item.disenchant_id is not None:
        columns.append(("disenchant_id", str(item.disenchant_id)))
    if item.block is not None:
        columns.append(("block", str(item.block)))
    if item.buy_price is not None:
        columns.append(("buy_price", str(item.buy_price)))
    if item.delay is not None:
        columns.append(("delay", str(item.delay)))
    if item.damage is not None:
        columns += [("dmg_min1", str(item.damage[0])), ("dmg_max1", str(item.damage[1]))]
    if item.resistances is not None:
        columns += [(column, str(value)) for column, value in item.resistances.items()]
    for k, (stat, value) in enumerate(stats, start=1):
        columns += [("stat_type%d" % k, str(stat)), ("stat_value%d" % k, str(value))]
    if item.kind == NEW:
        spells = [(s, SPELL_TRIGGER_ON_EQUIP) for s in on_equip_spells(item)]
        spells += [(0, 0)] * (MAX_ITEM_SPELLS - len(spells))
        for k, (spell, trigger) in enumerate(spells, start=1):
            columns += [("spellid_%d" % k, str(spell)), ("spelltrigger_%d" % k, str(trigger)),
                        ("spellcharges_%d" % k, "0"), ("spellppmrate_%d" % k, "0"), ("spellcooldown_%d" % k, "-1"),
                        ("spellcategory_%d" % k, "0"), ("spellcategorycooldown_%d" % k, "-1")]
    return columns


def _sets_spells(item):
    return bool(item.bonuses or item.equip_spells)


def _stock_spell_sql(item, temp):
    """The on-equip spells of a changed stock item: empty each on-equip slot of the stock row, then put each
    spell into the first empty slot. The other slots (on use, chance on hit) stay. With no empty slot left,
    the subquery returns two rows, and the SQL fails instead of losing the spell."""
    lines = []
    for k in range(1, MAX_ITEM_SPELLS + 1):
        lines.append("UPDATE `%s` SET %s WHERE `spelltrigger_%d`=%d;" % (
            temp, ", ".join("`%s_%d`=%s" % (column, k, empty) for column, empty in SPELL_SLOT), k, SPELL_TRIGGER_ON_EQUIP))
    first_empty = " ".join("WHEN `spellid_%d`=0 THEN %d" % (k, k) for k in range(1, MAX_ITEM_SPELLS + 1))
    for spell in on_equip_spells(item):
        values = dict(SPELL_SLOT, spellid=str(spell), spelltrigger=str(SPELL_TRIGGER_ON_EQUIP))
        lines += [
            "SET @forever_slot = (SELECT CASE %s END FROM `%s`);" % (first_empty, temp),
            fail_unless("@forever_slot IS NOT NULL", "forever_full"),
            "UPDATE `%s` SET %s;" % (temp, ", ".join(
                "`%s_%d`=IF(@forever_slot=%d, %s, `%s_%d`)" % (column, k, k, values[column], column, k)
                for k in range(1, MAX_ITEM_SPELLS + 1) for column, _ in SPELL_SLOT)),
        ]
    return lines


def server_sql(items):
    """The forever_item_template rows: each a copy of the newest stock row of its source item (patch 0 so that
    it loads on every patch) with the spec columns set."""
    lines = []
    for item in sorted(items, key=lambda i: i.entry):
        if item.kind == NEW:
            temp, comment = "forever_item_new", "-- Item %s (%d), from item %d." % (item.name, item.entry, item.source_item)
        else:
            temp, comment = "forever_item_stock", "-- Changed stock item %s (%d)." % (item.name, item.entry)
        lines += [
            "",
            comment,
            copy_item_sql(temp, item.source_item),
            # The source row must exist: with none, the SQL fails instead of losing the item (#230).
            fail_unless("(SELECT COUNT(*) FROM `%s`)=1" % temp, "forever_found"),
            "UPDATE `%s` SET %s;" % (temp, ", ".join("`%s`=%s" % kv for kv in item_columns(item))),
        ]
        if item.kind == STOCK and _sets_spells(item):
            lines += _stock_spell_sql(item, temp)
        lines += [
            "INSERT INTO `%s` SELECT * FROM `%s`;" % (TABLE, temp),
            "DROP TEMPORARY TABLE `%s`;" % temp,
        ]
    return lines
