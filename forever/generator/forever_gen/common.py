"""The helpers that every row module of the generator shares (review L7, L8, L10 of spec #225).

It imports no other module of the generator, so every module can import it at the top: no import cycle.
"""
import re
from collections import namedtuple

# The value of a spec column that holds no value. Each column says what no value means for it (keep the value
# of the source row, none, or not ported).
NO_VALUE = "-"
# The world database build of the stock rows (SUPPORTED_CLIENT_BUILD of the core).
CLIENT_BUILD = 5875
# Spell.dbc 1.12.1: 61-63 Effect, 64-66 EffectDieSides, 76-78 EffectBasePoints (the first effect of each).
SPELL_EFFECT1, SPELL_DIE_SIDES1, SPELL_BASE_POINTS1 = 61, 64, 76
# Spell.dbc 1.12.1 has 8 reagent slots.
MAX_REAGENTS = 8
# SpellFocusObject.dbc 1.12.1: 0 ID, 1 Name (enUS).
FOCUS_NAME = 1


class SpecError(Exception):
    """The spec breaks a rule. The generator writes nothing."""


# The source of a dungeon loot row (spec #240): a Wowhead Forever URL or the client build, and the day it was read.
SOURCE = re.compile(r"(https://nether\.wowhead\.com/tooltip/\w+/\d+\?dataEnv=16&locale=0|https://www\.wowhead\.com/forever/\S+"
                    r"|client \d+\.\d+\.\d+\.\d+)$")
DATE = re.compile(r"\d{4}-\d{2}-\d{2}$")
# One line of the loot list (loot-list.tsv, #242): list (left-out, unconfirmed, unreadable, manual-source), kind, id,
# name, detail.
Listed = namedtuple("Listed", "list kind id name detail")


def check_source(where, raw):
    """Refuses a row whose `source` is no Wowhead Forever URL and no 'client <build>', or whose `date` is not
    YYYY-MM-DD."""
    if not SOURCE.match(raw["source"]):
        raise SpecError("%s has no source: '%s' is no Wowhead Forever URL and no 'client <build>'" % (where, raw["source"]))
    if not DATE.match(raw["date"]):
        raise SpecError("%s: the date '%s' is not YYYY-MM-DD" % (where, raw["date"]))


def rows_of(path, lines, header):
    """(where, {column: value}) of each row of a spec file whose column line is `header`."""
    rows = []
    for number, line in enumerate(lines[1:], start=2):
        values = line.split("\t")
        if len(values) != len(header):
            raise SpecError("%s: data line %d has %d columns, not %d" % (path, number, len(values), len(header)))
        raw = dict(zip(header, values))
        rows.append(("%s (data line %d)" % (raw["name"], number), raw))
    return rows


def optional_int(raw, column):
    """The number in `column` of a spec row, or None for NO_VALUE (the column keeps the value of the source row)."""
    return None if raw[column] == NO_VALUE else int(raw[column])


def int32(value):
    """A DBC uint32 field read as the signed value that it holds."""
    return value - (1 << 32) if value >= 1 << 31 else value


def sql_text(value):
    """A MySQL string literal."""
    return "'" + value.replace("\\", "\\\\").replace("'", "\\'") + "'"


def fail_unless(condition, variable):
    """SQL that fails the load (a subquery with two rows) unless `condition` holds. The generator has no world
    database: this is how its SQL refuses a stock row that is not as the spec expects."""
    return "SELECT IF(%s, 0, (SELECT 1 UNION SELECT 2)) INTO @%s;" % (condition, variable)


def copy_item_sql(temp, entry):
    """SQL that copies the newest stock item_template row of `entry` (the highest patch) into the new temporary
    table `temp`."""
    return ("CREATE TEMPORARY TABLE `%s` SELECT * FROM `item_template` WHERE `entry`=%d "
            "ORDER BY `patch` DESC LIMIT 1;" % (temp, entry))


def check_kind(where, kind, kinds):
    """Refuses a row whose kind is none of `kinds`."""
    if kind not in kinds:
        raise SpecError("%s: unknown kind '%s'" % (where, kind))


def number_text(value):
    """A number with at most two decimals and no trailing zeros: 1, 0.5, 0.25."""
    return ("%.2f" % value).rstrip("0").rstrip(".")


def id_range(value):
    """A free ID range of the spec, or the empty range (0, -1) when the spec names none."""
    return value or (0, -1)


def focus_ids(stock):
    """{name: ID} of the stock SpellFocusObject.dbc."""
    return {stock.spell_focus.text(row[FOCUS_NAME]): row[0] for row in stock.spell_focus.rows}
