"""The item set rows of the spec (item_sets.tsv, #231): new item sets and changed stock item sets.

The core and the client both read the item sets from ItemSet.dbc: the core the set bonuses (Item.cpp
AddItemsSetItem: each spell whose threshold the worn pieces reach), the client the tooltip (the name, the
pieces, the bonuses). With Forever.Rules = 1 the core reads ItemSet.dbc from the Forever DBC folder
(Forever.DataDir), so the generator writes the same table into the Forever DBC folder and the client data
patch: every stock row, the changed stock sets, the new sets. No server table holds the sets.

Set membership on the server is item_template.set_id. A piece that is an item row of the spec gets the set
ID in its forever_item_template row (server_sql). A stock piece keeps its stock row, so a stock piece can
only be named in its own stock set.

Every bonus is a stock 1.12 equip spell, named as in items.tsv (equip_stats.py) plus the set bonuses of
SET_BONUSES. A Forever bonus with no 1.12 aura is a bonus row with the bonuses "none": the spec keeps it, the
generator leaves it out.

The item sets of the dungeon loot (loot_item_sets.tsv, #248, spec #240) have the same rows plus `leftOut`, `source`
and `date` (LOOT_COLUMNS). A bonus that is not ported, or only in part, names its Forever value in `leftOut`; a set
whose `leftOut` names a reason stays out as a whole (resolve). The generator lists each in the loot list.
"""
from collections import namedtuple

from . import equip_stats
from .common import NO_VALUE, SpecError, rows_of
from .loot import DATE, SOURCE, Listed

COLUMNS = ("kind", "set", "name", "threshold", "pieces", "bonuses", "foreverSpell", "notes")
# loot_item_sets.tsv (#248): every row names its source and date, and what stays off.
LOOT_COLUMNS = ("kind", "set", "name", "threshold", "pieces", "bonuses", "foreverSpell", "leftOut", "source", "date",
                "notes")
# set: a new set (an ID that is no stock set); stockset: a changed stock set (a stock ID); bonus: a set bonus.
SET, STOCK_SET, BONUS = "set", "stockset", "bonus"
NOT_PORTED = "none"

# The bonuses of the sets that no item uses as a bonus (equip_stats.BONUSES has the others). The spell of a
# value is the one that most stock items use for it, then the lowest ID. No stock item gives Strength by a
# spell; 14492 "Increased Strength 20" is the stock equip-style spell (passive, no duration). 14494 "Increased
# Strength 22" is the same kind (#248, set 2131).
SET_BONUSES = dict(equip_stats.BONUSES, **{
    "Strength": (((29, 0),), {20: 14492, 22: 14494}),
    # The stock spells of the dungeon sets (#248): the Forever bonus is the stock spell itself.
    "Intellect": (((29, 3),), {10: 9106}),
    "ArcaneRes": (((22, 64),), {5: 14707}),
    "DaggerSkill": (((30, 173),), {1: 7534}),
    "ShadowRes": (((22, 32),), {10: 14673, 12: 14675, 14: 14677}),
    # Vitality: health every 5 seconds, also in combat.
    "HealthRegen": (((161, 0),), {
        2: 21346, 3: 21593, 4: 21347, 5: 21596, 6: 21348, 7: 21601, 8: 20885, 10: 21607, 16: 23210
    }),
})

# ItemSet.dbc 1.12.1: 0 ID, 1-8 Name (enUS first), 9 the name flags, 10-26 ItemID[17], 27-34 SetSpellID[8],
# 35-42 SetThreshold[8], 43 RequiredSkill, 44 RequiredSkillRank.
NAME, NAME_FLAGS, ITEMS, SPELLS, THRESHOLDS = 1, 9, 10, 27, 35
MAX_PIECES, MAX_SET_SPELLS = 17, 8

# set_id: the ItemSet.dbc ID; pieces: [item entry]; bonuses: [SetBonus]; notes: the set row's notes;
# kind: SET or STOCK_SET; left_out: why the whole set stays out, else None (#248).
ItemSet = namedtuple("ItemSet", "set_id name pieces bonuses notes kind left_out", defaults=(None,))
# threshold: the number of worn pieces; bonuses: [equip_stats.Bonus] ([] when not ported); name: the label;
# left_out: the Forever value that stays off, else None (#248).
SetBonus = namedtuple("SetBonus", "threshold bonuses forever_spell notes name left_out", defaults=("", None))


def read_rows(path, lines, columns=COLUMNS):
    sets, bonuses = {}, []
    for where, raw in rows_of(path, lines, columns):
        set_id = int(raw["set"])
        left_out = raw.get("leftOut", NO_VALUE)
        left_out = None if left_out == NO_VALUE else left_out
        if columns == LOOT_COLUMNS:
            _source(where, raw)
        if raw["kind"] in (SET, STOCK_SET):
            if set_id in sets:
                raise SpecError("%s: item set %d is in the spec twice" % (where, set_id))
            pieces = [int(p) for p in raw["pieces"].split(",")]
            sets[set_id] = ItemSet(set_id, raw["name"], pieces, [], raw["notes"], raw["kind"], left_out)
        elif raw["kind"] == BONUS:
            parts = [] if raw["bonuses"] == NOT_PORTED else equip_stats.parse(where, raw["bonuses"], SET_BONUSES)
            if raw["foreverSpell"] == NO_VALUE:
                raise SpecError("%s: a set bonus has no source (foreverSpell)" % where)
            if columns == LOOT_COLUMNS and not parts and left_out is None:
                raise SpecError("%s: the set bonus %s is left off (none) and names no Forever value (leftOut)"
                                % (where, raw["name"]))
            bonuses.append((where, set_id, SetBonus(int(raw["threshold"]), parts, int(raw["foreverSpell"]), raw["notes"],
                                                    raw["name"], left_out)))
        else:
            raise SpecError("%s: unknown kind '%s'" % (where, raw["kind"]))
    for where, set_id, bonus in bonuses:
        if set_id not in sets:
            raise SpecError("%s: a bonus of item set %d, which has no set row" % (where, set_id))
        sets[set_id].bonuses.append(bonus)
    return {"item_sets": [sets[k] for k in sorted(sets)]}


def read_loot_rows(path, lines):
    """The rows of loot_item_sets.tsv (#248)."""
    return read_rows(path, lines, LOOT_COLUMNS)


def _source(where, raw):
    where = "item set row %s" % where
    if not SOURCE.match(raw["source"]):
        raise SpecError("%s has no source: '%s' is no Wowhead Forever URL and no 'client <build>'" % (where, raw["source"]))
    if not DATE.match(raw["date"]):
        raise SpecError("%s: the date '%s' is not YYYY-MM-DD" % (where, raw["date"]))


def resolve(spec):
    """(the spec without the left-out sets, the Listed rows): each left-out set with every bonus, and each bonus of
    a kept set that leaves a Forever value off."""
    listed, kept = [], []
    for item_set in spec.item_sets:
        if item_set.left_out is not None:
            listed.append(Listed("left-out", "set", item_set.set_id, item_set.name, item_set.left_out))
        else:
            kept.append(item_set)
        for bonus in item_set.bonuses:
            if bonus.left_out is not None or item_set.left_out is not None:
                detail = bonus.left_out or "Forever %d (the set is left out)" % bonus.forever_spell
                listed.append(Listed("left-out", "set bonus", item_set.set_id, bonus.name, detail))
    return spec._replace(item_sets=kept), listed


def spells(item_set):
    """(spell, threshold) of each ported bonus, by threshold."""
    return [(b.spell, bonus.threshold) for bonus in sorted(item_set.bonuses, key=lambda b: b.threshold)
            for b in bonus.bonuses]


def check(spec, stock):
    """The set rules, on the spec after the dead-content rule: each piece is an item row of the spec or a
    stock piece of the same stock set, in one set only; at most 17 pieces and 8 spells; each threshold
    between 1 and the number of pieces; the bonus table proven against the stock Spell.dbc. A new set has
    no stock set ID and a changed stock set has one (an ID clash, US35)."""
    equip_stats.check(stock, SET_BONUSES)
    stock_sets = stock.item_set.by_id()
    spec_items = {item.entry for item in spec.items}
    owner, seen = {}, set()
    for item_set in spec.item_sets:
        where = "item set %s (%d)" % (item_set.name, item_set.set_id)
        if item_set.set_id in seen:
            raise SpecError("%s: item set %d is in the spec twice" % (where, item_set.set_id))
        seen.add(item_set.set_id)
        if item_set.kind == SET and item_set.set_id in stock_sets:
            raise SpecError("%s: a new set (kind set) has the ID of a stock set" % where)
        if item_set.kind == STOCK_SET and item_set.set_id not in stock_sets:
            raise SpecError("%s: a changed stock set (kind stockset) names no stock set" % where)
        stock_pieces = set(stock_sets[item_set.set_id][ITEMS:ITEMS + MAX_PIECES]) - {0} if item_set.set_id in stock_sets else set()
        if len(item_set.pieces) > MAX_PIECES:
            raise SpecError("%s: %d pieces, at most %d" % (where, len(item_set.pieces), MAX_PIECES))
        for piece in item_set.pieces:
            if piece in owner:
                raise SpecError("%s: item %d is a piece of item set %d too" % (where, piece, owner[piece]))
            owner[piece] = item_set.set_id
            if piece not in spec_items and piece not in stock_pieces:
                raise SpecError("%s: item %d is no item row of the spec (or the dead-content rule cut it) and no "
                                "stock piece of this set" % (where, piece))
        set_spells = spells(item_set)
        if len(set_spells) > MAX_SET_SPELLS:
            raise SpecError("%s: %d bonus spells, at most %d" % (where, len(set_spells), MAX_SET_SPELLS))
        for bonus in item_set.bonuses:
            if not 1 <= bonus.threshold <= len(item_set.pieces):
                raise SpecError("%s: the threshold %d is not 1 to %d (the pieces)" % (where, bonus.threshold, len(item_set.pieces)))


def client_table(spec, stock):
    """ItemSet.dbc: every stock row, a changed stock set in place, a new set as a new row; sorted by ID. A
    stock row that the spec does not name stays byte for byte."""
    table = stock.item_set.copy()
    by_id = table.by_id()
    for item_set in spec.item_sets:
        row = by_id.get(item_set.set_id)
        if row is None:
            row = [0] * table.fields
            row[0] = item_set.set_id
            # The name flags of a stock row (the client's locale mask of the name).
            row[NAME_FLAGS] = stock.item_set.rows[0][NAME_FLAGS]
            table.rows.append(row)
        if row[NAME] == 0 or table.text(row[NAME]) != item_set.name:
            row[NAME:NAME + 8] = [table.add_text(item_set.name)] + [0] * 7
        pieces = item_set.pieces + [0] * (MAX_PIECES - len(item_set.pieces))
        row[ITEMS:ITEMS + MAX_PIECES] = pieces
        set_spells = spells(item_set)
        pairs = set_spells + [(0, 0)] * (MAX_SET_SPELLS - len(set_spells))
        row[SPELLS:SPELLS + MAX_SET_SPELLS] = [s for s, _ in pairs]
        row[THRESHOLDS:THRESHOLDS + MAX_SET_SPELLS] = [t for _, t in pairs]
    table.rows.sort(key=lambda r: r[0])
    return table


def server_sql(spec, item_table):
    """The set_id of each piece that is an item row of the spec, on its row of `item_table`."""
    spec_items = {item.entry for item in spec.items}
    lines = []
    for item_set in spec.item_sets:
        pieces = [p for p in item_set.pieces if p in spec_items]
        if pieces:
            lines += ["", "-- The pieces of item set %s (%d)." % (item_set.name, item_set.set_id),
                      "UPDATE `%s` SET `set_id`=%d WHERE `entry` IN (%s);" % (
                          item_table, item_set.set_id, ", ".join(str(p) for p in sorted(pieces)))]
    return lines
