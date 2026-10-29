"""The rules that a spec must keep, so that it cannot break a realm. check() raises a SpecError with the
first broken rule; the generator then writes nothing.

Tree rules (the stock 1.12 talent window and the core's talent learning): at most 7 tiers, 4 columns and
20 talents per tree, at most 5 ranks, at most one prerequisite (the core checks only the first one), a
prerequisite in the same tree on a lower tier, and one talent per cell.
ID rules: no clash with stock data. A talent keeps its own stock talent ID or takes a new one from the free
range. A stock spell can only be changed in place (it is its own source), and a talent only changes the
stock rank spells of its own stock talent. A new spell takes a new ID from the free range. No ID twice.
Icon rule: a stock SpellIcon.dbc ID, and never a core spell marker on a new or changed icon.
"""
from collections import Counter

from .spec import SpecError, talents

MAX_TIERS, MAX_COLUMNS, MAX_TALENTS_PER_TREE, MAX_RANKS = 7, 4, 20, 5
WARRIOR_TABS = {"arms": 161, "fury": 164, "protection": 163}
TALENT_RANKS = slice(4, 9)
SPELL_ICON = 117


def check_tree(spec):
    rows = talents(spec)
    by_id = {}
    for row in rows:
        if row.talent_id in by_id:
            raise SpecError("talent %d is in the spec twice" % row.talent_id)
        by_id[row.talent_id] = row
    for tree, count in Counter(row.tree for row in rows).items():
        if count > MAX_TALENTS_PER_TREE:
            raise SpecError("the %s tree has %d talents: more than %d talents per tree" % (tree, count, MAX_TALENTS_PER_TREE))
    cells = {}
    for row in rows:
        where = "talent %d (%s)" % (row.talent_id, row.name)
        if not 1 <= row.tier <= MAX_TIERS:
            raise SpecError("%s: tier %d, the tiers are 1-%d" % (where, row.tier, MAX_TIERS))
        if not 1 <= row.column <= MAX_COLUMNS:
            raise SpecError("%s: column %d, the columns are 1-%d" % (where, row.column, MAX_COLUMNS))
        if not 1 <= row.ranks <= MAX_RANKS:
            raise SpecError("%s: %d ranks, at most %d ranks" % (where, row.ranks, MAX_RANKS))
        cell = (row.tree, row.tier, row.column)
        if cell in cells:
            raise SpecError("%s: tier %d column %d of %s is also talent %d" % (where, row.tier, row.column, row.tree, cells[cell]))
        cells[cell] = row.talent_id
        if len(row.required_talents) > 1:
            raise SpecError("%s: %d prerequisites, at most one prerequisite" % (where, len(row.required_talents)))
        if row.required_talents:
            required = by_id.get(row.required_talents[0])
            if required is None or required.tree != row.tree or required.tier >= row.tier:
                raise SpecError("%s: the prerequisite %d is no talent of a lower tier of the %s tree"
                                % (where, row.required_talents[0], row.tree))
            if not 1 <= row.required_rank <= required.ranks:
                raise SpecError("%s: prerequisite rank %d of %d ranks" % (where, row.required_rank, required.ranks))
        elif row.required_rank:
            raise SpecError("%s: a prerequisite rank without a prerequisite" % where)


def check_ids(spec, stock):
    low, high = spec.id_range
    stock_talents = stock.talent.by_id()
    stock_spells = stock.spell.by_id()
    for row in talents(spec):
        old = stock_talents.get(row.talent_id)
        if old is not None and old[1] not in WARRIOR_TABS.values():
            raise SpecError("talent %d (%s) clashes with a stock talent of another class" % (row.talent_id, row.name))
        if old is None and not low <= row.talent_id <= high:
            raise SpecError("talent %d (%s) is no 1.12 Warrior talent and is outside the free ID range %d-%d"
                            % (row.talent_id, row.name, low, high))
    seen = {}
    for row in spec.rows:
        own_stock = set()
        if row.kind == "talent" and row.talent_id in stock_talents:
            own_stock = set(stock_talents[row.talent_id][TALENT_RANKS]) - {0}
        for rank, (spell, source) in enumerate(zip(row.spells, row.source_spells)):
            where = "%s rank %d: spell %d" % (row.name, rank + 1, spell)
            if spell in seen:
                raise SpecError("%s clashes with %s, which has the same ID" % (where, seen[spell]))
            seen[spell] = row.name
            if source not in stock_spells:
                raise SpecError("%s: the source spell %d is no stock spell" % (where, source))
            if spell in stock_spells:
                if row.kind == "ability":
                    raise SpecError("%s clashes with a stock spell: a new ability takes a new ID" % where)
                if spell != source:
                    raise SpecError("%s clashes with a stock spell: a stock spell is its own source" % where)
                if row.kind == "talent" and spell not in own_stock:
                    raise SpecError("%s clashes with a stock spell that is no rank of this talent" % where)
            else:
                if row.kind == "base":
                    raise SpecError("%s is no stock spell: a base row changes stock spells" % where)
                if not low <= spell <= high:
                    raise SpecError("%s is outside the free ID range %d-%d" % (where, low, high))


def check_icons(spec, stock, marker_icons):
    stock_icons = set(stock.spell_icon.by_id())
    stock_spells = stock.spell.by_id()
    for row in spec.rows:
        if row.icon not in stock_icons:
            raise SpecError("%s: icon %d is no stock SpellIcon.dbc ID" % (row.name, row.icon))
        if row.icon not in marker_icons:
            continue
        for spell in row.spells:
            if spell not in stock_spells or stock_spells[spell][SPELL_ICON] != row.icon:
                raise SpecError("%s: spell %d gets icon %d, which the core uses as a spell marker"
                                % (row.name, spell, row.icon))


def check(spec, stock, marker_icons):
    check_tree(spec)
    check_ids(spec, stock)
    check_icons(spec, stock, marker_icons)
