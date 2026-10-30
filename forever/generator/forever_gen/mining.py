"""The Mining rows of the spec (mining.tsv, #232): the new ores and their drops from the stock mining veins.

An ore row is a new item (items.Item of kind item): items.py checks it and writes it into forever_item_template
like every other new item. A drop row adds an ore to the loot of one vein loot ID: a forever_gameobject_loot_template
row. The core reads that table over gameobject_loot_template by `entry` (the loot ID), and a loot ID with a Forever
row loses all its stock rows (Forever::Table). So the generator first copies every stock row of the loot ID, then
adds the ore. A cut row names Forever mining content that is not ported (the Heavy Thorium Vein); it goes into the
cut list only.

The generator has no world DB: the SQL fails when a vein of a drop row does not use its loot ID.
"""
from collections import namedtuple

from . import dead_content, items as item_rows
from .spec import SpecError, rows_of

COLUMNS = ("kind", "item", "name", "sourceItem", "quality", "itemLevel", "displayId", "sellPrice", "foreverItem",
           "vein", "gameobjects", "lootId", "chance", "groupid", "rate", "notes")
ORE, DROP, CUT = "ore", "drop", "cut"
KINDS = (ORE, DROP, CUT)
# spec: the spec's own rate; fallback: a stand-in until launch data gives one.
RATES = ("spec", "fallback")
TABLE = "forever_gameobject_loot_template"
STOCK_TABLE = "gameobject_loot_template"

# One ore in the loot of one vein loot ID. gameobjects: the stock gameobject entries that use the loot ID.
Drop = namedtuple("Drop", "item name vein gameobjects loot_id chance group rate notes")
# Forever mining content that is not ported: the Forever ID it names, its name and why.
MiningCut = namedtuple("MiningCut", "id name reason")


def read_rows(path, lines):
    ores, drops, cuts = [], [], []
    for where, raw in rows_of(path, lines, COLUMNS):
        kind = raw["kind"]
        if kind not in KINDS:
            raise SpecError("%s: unknown kind '%s'" % (where, kind))
        if kind == ORE:
            ores.append(_ore(raw))
        elif kind == DROP:
            drops.append(_drop(where, raw))
        else:
            cuts.append(MiningCut(int(raw["item"]), raw["name"], raw["notes"]))
    return {"items": ores, "drops": drops, "mining_cuts": cuts}


def _ore(raw):
    return item_rows.Item(
        kind=item_rows.NEW, entry=int(raw["item"]), source_item=int(raw["sourceItem"]), name=raw["name"],
        quality=item_rows.QUALITIES[raw["quality"]], item_level=int(raw["itemLevel"]), required_level=0,
        bonding=item_rows.BONDINGS["none"], inventory_type=None, delay=None, damage=None, armor=0, resistances=None,
        stats=[], bonuses=[], equip_spells=[], display_id=int(raw["displayId"]), sell_price=int(raw["sellPrice"]),
        forever_item=int(raw["foreverItem"]), forever_stats="-", notes=raw["notes"])


def _drop(where, raw):
    if raw["rate"] not in RATES:
        raise SpecError("%s: rate '%s' is none of %s" % (where, raw["rate"], ", ".join(RATES)))
    chance = float(raw["chance"])
    if not 0 < chance <= 100:
        raise SpecError("%s: chance %s is not above 0 and at most 100" % (where, raw["chance"]))
    return Drop(item=int(raw["item"]), name=raw["name"], vein=raw["vein"],
                gameobjects=[int(g) for g in raw["gameobjects"].split(",")], loot_id=int(raw["lootId"]),
                chance=chance, group=int(raw["groupid"]), rate=raw["rate"], notes=raw["notes"])


def check(spec, stock):
    """A drop names an item (a spec item or a stock item) and each loot ID drops an item once (the key of the
    loot table)."""
    spec_items = {item.entry for item in spec.items}
    seen = set()
    for drop in spec.drops:
        where = "drop %s from loot ID %d" % (drop.name, drop.loot_id)
        if drop.item not in spec_items and drop.item not in stock.item_ids:
            raise SpecError("%s: item %d is no item (no spec item and no stock item)" % (where, drop.item))
        if (drop.loot_id, drop.item) in seen:
            raise SpecError("%s: loot ID %d drops item %d twice" % (where, drop.loot_id, drop.item))
        seen.add((drop.loot_id, drop.item))


def cut(spec, full_items):
    """(the spec without the drops of new items that the dead-content rule cut, the Cut rows of those drops and
    of the cut rows). full_items: the items of the spec before the rule."""
    live = {item.entry for item in spec.items}
    new = {item.entry for item in full_items if item.kind == item_rows.NEW}
    dead = [d for d in spec.drops if d.item in new and d.item not in live]
    cuts = [dead_content.Cut("drop", d.loot_id, "%s from %s" % (d.name, d.vein),
                             "drops %s (%d), which the dead-content rule cut" % (d.name, d.item)) for d in dead]
    cuts += [dead_content.Cut("mining", c.id, c.name, c.reason) for c in spec.mining_cuts]
    return spec._replace(drops=[d for d in spec.drops if d not in dead]), cuts


def server_sql(spec):
    """The forever_gameobject_loot_template rows: per loot ID, a check of its veins, a copy of its stock rows and
    its ore rows. They are copied with SELECT *, so they come before the removal marker column (removals.py)."""
    lines = []
    by_loot = {}
    for drop in spec.drops:
        by_loot.setdefault(drop.loot_id, []).append(drop)
    for loot_id in sorted(by_loot):
        drops = sorted(by_loot[loot_id], key=lambda d: d.item)
        gameobjects = sorted({g for d in drops for g in d.gameobjects})
        lines += [
            "",
            "-- Loot ID %d (%s): the stock rows and %s." % (loot_id, drops[0].vein, ", ".join(d.name for d in drops)),
            # Each vein of the row must use this loot ID: with one that does not, the SQL fails.
            "SELECT IF((SELECT COUNT(DISTINCT `entry`) FROM `gameobject_template` WHERE `entry` IN (%s) AND `data1`=%d)=%d, "
            "0, (SELECT 1 UNION SELECT 2)) INTO @forever_vein;" % (", ".join(map(str, gameobjects)), loot_id, len(gameobjects)),
            "INSERT INTO `%s` SELECT * FROM `%s` WHERE `entry`=%d;" % (TABLE, STOCK_TABLE, loot_id),
        ]
        lines += ["INSERT INTO `%s` (`entry`, `item`, `ChanceOrQuestChance`, `groupid`, `mincountOrRef`, `maxcount`) "
                  "VALUES (%d, %d, %s, %d, 1, 1);" % (TABLE, loot_id, d.item, _chance(d.chance), d.group) for d in drops]
    return lines


def _chance(value):
    return ("%.2f" % value).rstrip("0").rstrip(".")
