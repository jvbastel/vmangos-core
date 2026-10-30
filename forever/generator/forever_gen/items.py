"""The item rows of the spec (items.tsv, #226): new items, and later changed stock items.

The 1.12 client has no item table: it asks the server for each item. So an item is server data only: a row
of forever_item_template, which the core reads with Forever.Rules = 1 over the stock item_template rows by
entry. The generator writes it as a copy of the stock row of its source item (class, subclass, slot,
material, durability, sheath) with the spec values on top, so that a new column of item_template cannot
break it.
"""
from collections import namedtuple

from .spec import SpecError, rows_of

# stats: [(item_template stat type, value)]; equip_spells: [spell ID]; quality and bonding: the numbers.
Item = namedtuple("Item", "kind entry source_item name quality item_level required_level bonding armor stats "
                          "equip_spells display_id sell_price forever_item forever_stats notes")

COLUMNS = ("kind", "entry", "sourceItem", "name", "quality", "itemLevel", "requiredLevel", "bonding", "armor",
           "stats", "equipSpells", "displayId", "sellPrice", "foreverItem", "foreverStats", "notes")
KINDS = ("item",)
QUALITIES = {"poor": 0, "common": 1, "uncommon": 2, "rare": 3, "epic": 4}
BONDINGS = {"none": 0, "pickup": 1, "equip": 2, "use": 3}
# The item_template stat types of the 1.12 base stats.
STAT_TYPES = {"Mana": 0, "Health": 1, "Agi": 3, "Str": 4, "Int": 5, "Spi": 6, "Sta": 7}
MAX_STATS, MAX_SPELLS = 10, 5
SPELL_TRIGGER_ON_EQUIP = 1
TABLE = "forever_item_template"


def _stats(where, text):
    if text == "-":
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


def _named(where, column, text, names):
    if text not in names:
        raise SpecError("%s: %s '%s' is none of %s" % (where, column, text, ", ".join(names)))
    return names[text]


def read_rows(path, lines):
    items = []
    for where, raw in rows_of(path, lines, COLUMNS):
        if raw["kind"] not in KINDS:
            raise SpecError("%s: unknown kind '%s'" % (where, raw["kind"]))
        spells = [] if raw["equipSpells"] == "-" else [int(s) for s in raw["equipSpells"].split(",")]
        if len(spells) > MAX_SPELLS:
            raise SpecError("%s: %d equip spells, at most %d" % (where, len(spells), MAX_SPELLS))
        items.append(Item(
            kind=raw["kind"], entry=int(raw["entry"]), source_item=int(raw["sourceItem"]), name=raw["name"],
            quality=_named(where, "quality", raw["quality"], QUALITIES), item_level=int(raw["itemLevel"]),
            required_level=int(raw["requiredLevel"]), bonding=_named(where, "bonding", raw["bonding"], BONDINGS),
            armor=int(raw["armor"]), stats=_stats(where, raw["stats"]), equip_spells=spells,
            display_id=int(raw["displayId"]), sell_price=int(raw["sellPrice"]),
            forever_item=int(raw["foreverItem"]), forever_stats=raw["foreverStats"], notes=raw["notes"]))
    return {"items": items}


def check(spec, stock):
    """The ID rules of the items: a new item (not its own source) in the free item range, no entry twice,
    and stock equip spells."""
    low, high = spec.item_id_range or (0, -1)
    stock_spells = stock.spell.by_id()
    seen = set()
    for item in spec.items:
        where = "item %s: entry %d" % (item.name, item.entry)
        if item.entry in seen:
            raise SpecError("%s is in the spec twice" % where)
        seen.add(item.entry)
        if item.entry != item.source_item and not low <= item.entry <= high:
            raise SpecError("%s is outside the free item ID range %d-%d" % (where, low, high))
        for spell in item.equip_spells:
            if spell not in stock_spells:
                raise SpecError("%s: the equip spell %d is no stock spell" % (where, spell))


def _sql_text(value):
    return "'" + value.replace("\\", "\\\\").replace("'", "\\'") + "'"


def item_columns(item):
    """The item_template columns that the spec sets on the copy of the source row, in this order. Every stat
    and spell slot is set, so that none of the source item stays."""
    stats = item.stats + [(0, 0)] * (MAX_STATS - len(item.stats))
    spells = [(s, SPELL_TRIGGER_ON_EQUIP) for s in item.equip_spells] + [(0, 0)] * (MAX_SPELLS - len(item.equip_spells))
    columns = [("entry", str(item.entry)), ("patch", "0"), ("name", _sql_text(item.name)),
               ("description", "''"), ("display_id", str(item.display_id)), ("quality", str(item.quality)),
               ("sell_price", str(item.sell_price)), ("item_level", str(item.item_level)),
               ("required_level", str(item.required_level)), ("bonding", str(item.bonding)),
               ("armor", str(item.armor)), ("set_id", "0"), ("random_property", "0")]
    for k, (stat, value) in enumerate(stats, start=1):
        columns += [("stat_type%d" % k, str(stat)), ("stat_value%d" % k, str(value))]
    for k, (spell, trigger) in enumerate(spells, start=1):
        columns += [("spellid_%d" % k, str(spell)), ("spelltrigger_%d" % k, str(trigger)),
                    ("spellcharges_%d" % k, "0"), ("spellppmrate_%d" % k, "0"), ("spellcooldown_%d" % k, "-1"),
                    ("spellcategory_%d" % k, "0"), ("spellcategorycooldown_%d" % k, "-1")]
    return columns


def server_sql(items):
    """The forever_item_template rows: each a copy of the newest stock row of its source item (patch 0 so that
    it loads on every patch) with the spec columns set."""
    lines = []
    for item in sorted(items, key=lambda i: i.entry):
        lines += [
            "",
            "-- Item %s (%d), from item %d." % (item.name, item.entry, item.source_item),
            "CREATE TEMPORARY TABLE `forever_item_new` SELECT * FROM `item_template` WHERE `entry`=%d "
            "ORDER BY `patch` DESC LIMIT 1;" % item.source_item,
            "UPDATE `forever_item_new` SET %s;" % ", ".join("`%s`=%s" % kv for kv in item_columns(item)),
            "INSERT INTO `%s` SELECT * FROM `forever_item_new`;" % TABLE,
            "DROP TEMPORARY TABLE `forever_item_new`;",
        ]
    return lines
