"""The removal marker (#229): an override row that takes a stock row away on the Forever realm.

Each forever_<table> override table gets one more column, `forever_removed` (MARKER), after the shape of its
stock table. A row with the marker 1 is a copy of the stock row(s) that it removes, with the same key. The
core reads forever_<table> over <table> by one key column (Forever::Table and SelectWithForeverRows in
src/game/Forever/Forever.cpp): a stock row whose key has a forever row does not load, and a forever row with
the marker does not load either. So the stock row is gone with the rules on, and loads as usual with the
rules off (the core then reads no forever table).

How a ticket removes stock rows (any override table but those of NOT_REMOVABLE, which get no marker column:
skill line abilities, trainers, items, gameobject loot):
1. Its table is in build.FOREVER_TABLES, so that marker_sql gives it the column.
2. Its module returns remove_rows(<stock table>, <the key column of the core's Table() call>, keys) lines,
   and build.server_sql puts them in the `removed` list, which it writes after marker_sql at the very end.
3. Before the marker column, every forever row is copied with `INSERT INTO forever_x SELECT * FROM ...`. Such
   a copy after marker_sql would have one column too few, so all other rows come first
   (tests/test_removals.py checks it).
A key that a ticket removes must have no other forever row: the key is the union key of the whole table.
"""
from .spec import SpecError

MARKER = "forever_removed"
# The stock tables whose rows a marker must not remove, with the reason.
NOT_REMOVABLE = {
    # Player::AddSpell deletes the character_spell rows of an unknown spell for every character, and the core
    # loads forever_spell_template over the stock spells by ID (Forever::LoadSpellTemplate), not by Table().
    # Take a recipe away by its skill line ability and its trainer rows instead (removed_recipes.py).
    "spell_template": "a removed spell would delete the spell of every character who knows it",
}


def marker_sql(tables):
    """The marker column of each override table in `tables` (forever_<stock table>) whose stock rows can be
    removed. It comes after every other row of the tables (see the module docstring). A table of
    NOT_REMOVABLE gets no marker: its core loader would not honour it (forever_spell_template is read by
    position with SELECT *, by Forever::LoadSpellTemplate, not by Table())."""
    lines = ["", "-- The removal marker (#229): a forever row with %s = 1 takes the stock rows of its key away." % MARKER]
    lines += ["ALTER TABLE `%s` ADD COLUMN `%s` TINYINT UNSIGNED NOT NULL DEFAULT 0;" % (table, MARKER)
              for table in tables if table[len("forever_"):] not in NOT_REMOVABLE]
    return lines


def remove_rows(table, key, keys, comment):
    """The SQL that removes every stock row of the stock table `table` whose column `key` is one of `keys`:
    a copy of those rows in forever_<table> with the marker 1. `key` is the key column of the core's Table()
    call on the table. No stock row of a key: no copy, and nothing to remove."""
    if table in NOT_REMOVABLE:
        raise SpecError("%s: the removal marker cannot remove %s rows: %s" % (comment, table, NOT_REMOVABLE[table]))
    if not keys:
        return []
    return ["-- %s" % comment,
            "INSERT INTO `forever_%s` SELECT *, 1 FROM `%s` WHERE `%s` IN (%s);"
            % (table, table, key, ", ".join(str(k) for k in sorted(keys)))]
