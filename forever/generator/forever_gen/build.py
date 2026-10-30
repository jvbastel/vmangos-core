"""Builds the Forever data from the spec and the stock 1.12.1 DBC files.

Three outputs, all from the same rows, so that the client and the server never drift apart:
  dbc/                  the Forever DBC folder of the server core (Forever.DataDir): the talent tables
  client/DBFilesClient/ the DBCs of the client data patch
  client/client-data-patch.MPQ  the client data patch: those DBCs in one MPQ
  sql/forever_world.sql the Forever server tables (forever_spell_template, ...) of the world database, and
                        forever_baseline_spell: the spells that the core teaches at their level (#212)
The client data patch holds the talent tables, the spell table and the skill line abilities.

Each row kind of the spec has its own functions: the spell rows here, the recipes in recipes.py and the
items in items.py. The spell rows and the recipes both become SpellEdits and Abilities, which the table
writers here turn into the client tables and the server SQL.
"""
import os
import struct
from collections import namedtuple

from . import client_patch, items, recipes, rules
from .dbc import Dbc
from .spec import (BASELINE, FIELDS, FLOAT_FIELDS, GENERATOR_KEYS, NAME_SUBTEXT, PROC_EVENT, SCRIPT_NAME, SKILL_LINE,
                   SUPERSEDED_BY, THREAT, proc_event, talents)

# TalentTab.dbc IDs of the Warrior trees.
WARRIOR_TABS = rules.WARRIOR_TABS

# Talent.dbc 1.12.1: 0 ID, 1 TabID, 2 Tier, 3 Column (from 0), 4-12 RankID[9], 13-15 DependsOn[3],
# 16-18 DependsOnRank[3] (0 = rank 1), 19 Flags, 20 DependsOnSpell.
TALENT_FIELDS = 21
# Spell.dbc 1.12.1 (the other fields: spec.FIELDS): 6 Attributes, 117 SpellIconID,
# 120-127 Name[8], 129-136 Rank[8], 138-145 Description[8] (index 0 = enUS).
SPELL_ATTRIBUTES, SPELL_ICON = 6, 117
SPELL_NAME, SPELL_RANK, SPELL_DESCRIPTION = 120, 129, 138
SPELL_ATTR_PASSIVE = 0x40
# The one 64-bit `set` column: the generator writes its high word into the next Spell.dbc field.
SPELL_FAMILY_FLAGS = "spellFamilyFlags"

# The world database build of the Forever rows (SUPPORTED_CLIENT_BUILD of the core).
CLIENT_BUILD = 5875
FOREVER_TABLES = ("forever_spell_template", "forever_spell_chain", "forever_spell_proc_event",
                  "forever_skill_line_ability", "forever_spell_threat", items.TABLE)
# The spells that the core teaches a Warrior at their spellLevel, with no trainer (#212). The core reads it only
# with Forever.Rules = 1. The migration sql/migrations/20261029120000_world.sql makes it too, with the same
# columns (tests/test_spell_ids.py checks it), so that a realm without the generated SQL fails at startup, not quietly.
BASELINE_TABLE = "forever_baseline_spell"
BASELINE_COLUMNS = "(`spell_id` SMALLINT UNSIGNED NOT NULL, PRIMARY KEY (`spell_id`))"
# SkillLineAbility.dbc 1.12.1: 0 ID, 1 SkillLine, 2 Spell, 3 RaceMask, 4 ClassMask, 5-6 exclude masks,
# 7 MinSkillRank, 8 SupersededBy, 9 AcquireMethod (0: a trainer or the core teaches it), 10 the skill from
# which a recipe is grey (TrivialSkillLineRankHigh), 11 the skill from which it is yellow (...RankLow), 12-14 0.
WARRIOR_CLASS_MASK = 1

# One spell of the client spell table and of forever_spell_template: a copy of the stock spell `source`
# with `changes` [(spell_template column, value)] set. rank_text None: no rank text change on a kept spell;
# description and script_name None: keep the source one. comment: the SQL comment line.
SpellEdit = namedtuple("SpellEdit", "spell source changes name rank_text description script_name comment")
# One skill line ability (client SkillLineAbility.dbc and forever_skill_line_ability). The ID is the spell ID.
# grey and yellow: the colour range of a recipe (0 for the Warrior spells).
Ability = namedtuple("Ability", "id skill spell race_mask class_mask req_skill superseded grey yellow")


class Stock:
    """The stock DBC files that the generator reads."""

    def __init__(self, folder):
        self.spell_focus = Dbc.read(os.path.join(folder, "SpellFocusObject.dbc"))
        self.talent = Dbc.read(os.path.join(folder, "Talent.dbc"))
        self.talent_tab = Dbc.read(os.path.join(folder, "TalentTab.dbc"))
        self.spell = Dbc.read(os.path.join(folder, "Spell.dbc"))
        self.spell_icon = Dbc.read(os.path.join(folder, "SpellIcon.dbc"))
        self.skill_line_ability = Dbc.read(os.path.join(folder, "SkillLineAbility.dbc"))


def spell_ranks(spec):
    """(spell ID, source spell ID, row, rank from 0) of each spell of the spec, by spell ID."""
    ranks = [(spell, source, row, rank) for row in spec.rows
             for rank, (spell, source) in enumerate(zip(row.spells, row.source_spells))]
    return sorted(ranks, key=lambda r: r[0])


def _rank_text(row, rank, stock_text):
    """The rank text of the spellbook: the `set` value, else "Rank N" for more than one rank, else the
    stock text of a kept stock spell (stock_text), else none."""
    if NAME_SUBTEXT in row.fields:
        return row.fields[NAME_SUBTEXT][rank]
    if row.ranks > 1:
        return "Rank %d" % (rank + 1)
    return stock_text


def spell_changes(row, rank):
    """The numeric spell_template columns that the spec sets on one rank: the spell icon, each effect value
    (as base points with die sides 1 and base dice 1) and the `set` columns, in this order."""
    changes = [("spellIconId", row.icon)]
    for k, values in enumerate(row.effects, start=1):
        if values is not None:
            changes += [("effectBasePoints%d" % k, values[rank] - 1), ("effectDieSides%d" % k, 1),
                        ("effectBaseDice%d" % k, 1)]
    changes += [(name, values[rank]) for name, values in row.fields.items()
                if name not in GENERATOR_KEYS and not name.startswith(PROC_EVENT)]
    return changes


def skill_line_abilities(spec):
    """The Ability of each spell of a row with a skill line, and of each recipe, by ID. The ID is the
    spell ID: a new spell is in the free range, so its ID is free in SkillLineAbility.dbc too."""
    abilities = [Ability(r.spell, r.skill_line, r.spell, 0, 0, r.min_rank, 0, r.grey, r.yellow) for r in spec.recipes]
    for row in spec.rows:
        if SKILL_LINE not in row.fields:
            continue
        for rank, spell in enumerate(row.spells):
            if SUPERSEDED_BY in row.fields:
                superseded = row.fields[SUPERSEDED_BY][rank]
            else:
                superseded = row.spells[rank + 1] if rank + 1 < len(row.spells) else 0
            abilities.append(Ability(spell, row.fields[SKILL_LINE][rank], spell, 0, WARRIOR_CLASS_MASK, 1, superseded, 0, 0))
    return sorted(abilities)


def baseline_spells(spec):
    return sorted(spell for row in spec.rows if BASELINE in row.fields
                  for rank, spell in enumerate(row.spells) if row.fields[BASELINE][rank])


def client_skill_line_ability_table(abilities, stock):
    """SkillLineAbility.dbc: every stock row byte for byte, then the new rows."""
    table = stock.skill_line_ability.copy()
    for a in abilities:
        row = [0] * table.fields
        row[0], row[1], row[2], row[3], row[4], row[7], row[8] = a.id, a.skill, a.spell, a.race_mask, a.class_mask, a.req_skill, a.superseded
        row[10], row[11] = a.grey, a.yellow
        table.rows.append(row)
    return table


def spell_texts(row, rank, stock_rank_text):
    """(name, rank text, description or None to keep the stock one) of one rank."""
    return row.name, _rank_text(row, rank, stock_rank_text), row.tooltip


def spell_edits(spec, stock):
    """The SpellEdit of each spell of the spec, by spell ID: the ranks of the spell rows, then the recipes."""
    stock_by_id = stock.spell.by_id()
    edits = []
    for spell, source, row, rank in spell_ranks(spec):
        stock_rank_text = stock.spell.text(stock_by_id[spell][SPELL_RANK]) if spell in stock_by_id else ""
        name, rank_text, description = spell_texts(row, rank, stock_rank_text)
        comment = "-- %s rank %d, %s." % (row.name, rank + 1,
                                          "from spell %d" % source if spell != source else "changes the stock spell")
        script = row.fields[SCRIPT_NAME][rank] if SCRIPT_NAME in row.fields else None
        edits.append(SpellEdit(spell, source, spell_changes(row, rank), name, rank_text, description, script, comment))
    for recipe in spec.recipes:
        edits.append(SpellEdit(recipe.spell, recipe.source_spell, recipes.spell_changes(recipe, stock), recipe.name,
                               "", None, None, "-- Recipe %s, from spell %d." % (recipe.name, recipe.source_spell)))
    return sorted(edits, key=lambda e: e.spell)


def talent_table(spec, stock):
    """Talent.dbc: the stock rows of the other classes, then the spec rows by ID.

    generate puts the rows in client order afterwards (in_client_order)."""
    stock_talents = stock.talent.by_id()
    stock_spells = stock.spell.by_id()
    rows = [list(row) for row in stock.talent.rows if row[1] not in WARRIOR_TABS.values()]
    for talent in sorted(talents(spec), key=lambda t: t.talent_id):
        old = stock_talents.get(talent.talent_id)
        # Flags 1 marks a talent that teaches an active ability. A kept talent keeps its stock value.
        flags = old[19] if old else int(not stock_spells[talent.source_spells[0]][SPELL_ATTRIBUTES] & SPELL_ATTR_PASSIVE)
        row = [0] * TALENT_FIELDS
        row[0] = talent.talent_id
        row[1] = WARRIOR_TABS[talent.tree]
        row[2] = talent.tier - 1
        row[3] = talent.column - 1
        row[4:4 + talent.ranks] = talent.spells
        if talent.required_talents:
            row[13] = talent.required_talents[0]
            row[16] = talent.required_rank - 1
        row[19] = flags
        rows.append(row)
    return Dbc(stock.talent.fields, rows, bytes(stock.talent.strings))


def in_client_order(talents):
    """The talent table in the row order of the stock table: by tree (TabID), tier, column. The 1.12
    talent window takes a tree's talents in row order and draws the prerequisite arrows as it goes."""
    talents.rows.sort(key=lambda row: (row[1], row[2], row[3]))
    return talents


def client_spell_table(edits, stock):
    """Spell.dbc: every stock row, the spec changes on the stock spells that the spec names, and the new
    spell rows, sorted by ID. A stock row whose values the spec does not change stays byte for byte."""
    table = stock.spell.copy()
    by_id = table.by_id()
    stock_by_id = stock.spell.by_id()
    for edit in edits:
        spell = edit.spell
        new = spell not in stock_by_id
        target = list(stock_by_id[edit.source]) if new else by_id[spell]
        target[0] = spell
        for name, value in edit.changes:
            target[FIELDS[name] if name != "spellIconId" else SPELL_ICON] = _dbc_value(name, value)
            if name == SPELL_FAMILY_FLAGS:
                target[FIELDS[name] + 1] = (value >> 32) & 0xFFFFFFFF
        for first, value in zip((SPELL_NAME, SPELL_RANK, SPELL_DESCRIPTION), (edit.name, edit.rank_text, edit.description)):
            # enUS only: the other locale strings of the stock row would be stale.
            if value is not None and (new or table.text(target[first]) != value):
                target[first:first + 8] = [table.add_text(value)] + [0] * 7
        if new:
            table.rows.append(target)
    table.rows.sort(key=lambda r: r[0])
    return table


def _dbc_value(name, value):
    """The raw 32-bit Spell.dbc field of a spec value: a float field as its float bits."""
    if name in FLOAT_FIELDS:
        return struct.unpack("<I", struct.pack("<f", value))[0]
    return value & 0xFFFFFFFF


def _sql_text(value):
    return "'" + value.replace("\\", "\\\\").replace("'", "\\'") + "'"


def server_sql(spec, edits):
    """The Forever server tables. Each spec spell is a copy of the stock server row of its source spell
    (with the core's own fixes), with the spec values set on top. The tables are made again in the shape
    of the stock tables first, so that a new column of a stock table cannot break the copies."""
    lines = ["-- The Forever server tables of the world database. Generated by forever/generator from",
             "-- the spec folder forever/spec: never edit by hand. The core reads them only with Forever.Rules = 1.", ""]
    for table in FOREVER_TABLES:
        lines += ["DROP TABLE IF EXISTS `%s`;" % table,
                  "CREATE TABLE `%s` LIKE `%s`;" % (table, table[len("forever_"):])]
    for edit in edits:
        values = [("entry", str(edit.spell)), ("build", str(CLIENT_BUILD)), ("name", _sql_text(edit.name)),
                  ("nameSubtext", _sql_text(edit.rank_text))]
        if edit.description is not None:
            values.append(("description", _sql_text(edit.description)))
        values += [(column, str(value)) for column, value in edit.changes]
        if edit.script_name is not None:
            values.append(("script_name", _sql_text(edit.script_name)))
        lines += [
            "",
            edit.comment,
            "CREATE TEMPORARY TABLE `forever_spell_new` SELECT * FROM `spell_template` WHERE `entry`=%d "
            "AND `build`<=%d ORDER BY `build` DESC LIMIT 1;" % (edit.source, CLIENT_BUILD),
            "UPDATE `forever_spell_new` SET %s;" % ", ".join("`%s`=%s" % kv for kv in values),
            "INSERT INTO `forever_spell_template` SELECT * FROM `forever_spell_new`;",
            "DROP TEMPORARY TABLE `forever_spell_new`;",
        ]
    for row in spec.rows:
        lines += proc_event_sql(row)
    lines += _server_threat(spec)
    lines += _server_skill_line_abilities(skill_line_abilities(spec))
    lines += _server_baseline(baseline_spells(spec))
    lines += items.server_sql(spec.items)
    return "\n".join(lines) + "\n"


def _server_threat(spec):
    """The forever_spell_threat rows: the flat threat of each rank of a row with the `threat` key. The core
    reads them over the stock spell_threat rows by entry (multiplier and build range: the table defaults)."""
    lines = []
    for row in spec.rows:
        if THREAT not in row.fields:
            continue
        lines += ["", "-- %s: the flat threat of each rank." % row.name,
                  "INSERT INTO `forever_spell_threat` (`entry`, `threat`) VALUES %s;" % ", ".join(
                      "(%d, %d)" % (spell, threat) for spell, threat in zip(row.spells, row.fields[THREAT]))]
    return lines


def _server_skill_line_abilities(abilities):
    """The forever_skill_line_ability rows: each a copy of a stock row (for the shape) with every column set."""
    lines = []
    for a in abilities:
        values = [("id", a.id), ("build", CLIENT_BUILD), ("skill_id", a.skill), ("spell_id", a.spell),
                  ("race_mask", a.race_mask), ("class_mask", a.class_mask), ("req_skill_value", a.req_skill),
                  ("superseded_by_spell", a.superseded), ("learn_on_get_skill", 0), ("max_value", a.grey),
                  ("min_value", a.yellow), ("req_train_points", 0)]
        lines += [
            "",
            "-- The skill line ability of spell %d." % a.spell,
            "CREATE TEMPORARY TABLE `forever_sla_new` SELECT * FROM `skill_line_ability` WHERE `build`=%d "
            "ORDER BY `id` LIMIT 1;" % CLIENT_BUILD,
            "UPDATE `forever_sla_new` SET %s;" % ", ".join("`%s`=%d" % kv for kv in values),
            "INSERT INTO `forever_skill_line_ability` SELECT * FROM `forever_sla_new`;",
            "DROP TEMPORARY TABLE `forever_sla_new`;",
        ]
    return lines


def _server_baseline(spells):
    lines = ["", "-- The baseline spells: the core teaches each at its spellLevel with no trainer.",
             "DROP TABLE IF EXISTS `%s`;" % BASELINE_TABLE,
             "CREATE TABLE `%s` %s;" % (BASELINE_TABLE, BASELINE_COLUMNS)]
    if spells:
        lines.append("INSERT INTO `%s` (`spell_id`) VALUES %s;" % (BASELINE_TABLE,
                                                                  ", ".join("(%d)" % s for s in spells)))
    return lines




def proc_event_sql(row):
    """The forever_spell_proc_event row of the first rank of a spec row that sets procEvent columns: a copy
    of the stock row of the spell (the core's build range), else a row with the table defaults."""
    values = proc_event(row)
    if not values:
        return []
    spell = row.spells[0]
    source = row.source_spells[0]
    return [
        "",
        "-- %s: the proc event of rank 1 (the core gives it to the higher ranks)." % row.name,
        "CREATE TEMPORARY TABLE `forever_proc_new` LIKE `spell_proc_event`;",
        "INSERT INTO `forever_proc_new` SELECT * FROM `spell_proc_event` WHERE `entry`=%d AND %d BETWEEN `build_min` "
        "AND `build_max`;" % (source, CLIENT_BUILD),
        "SET @forever_proc_rows = (SELECT COUNT(*) FROM `forever_proc_new`);",
        "INSERT INTO `forever_proc_new` (`entry`) SELECT %d FROM DUAL WHERE @forever_proc_rows = 0;" % source,
        "UPDATE `forever_proc_new` SET %s;" % ", ".join(
            "`%s`=%d" % kv for kv in [("entry", spell)] + sorted(values.items())),
        "INSERT INTO `forever_spell_proc_event` SELECT * FROM `forever_proc_new`;",
        "DROP TEMPORARY TABLE `forever_proc_new`;",
    ]


def generate(spec, stock_folder, out, marker_icons):
    """Checks the spec against the stock data and the core's marker icons, then writes the three outputs
    into `out`. A spec that breaks a rule raises a SpecError before anything is written."""
    stock = Stock(stock_folder)
    rules.check(spec, stock, marker_icons)
    recipes.check(spec, stock, [spell for spell, _, _, _ in spell_ranks(spec)])
    items.check(spec, stock)
    edits = spell_edits(spec, stock)
    talents = in_client_order(talent_table(spec, stock))
    spell_table = client_spell_table(edits, stock)
    sql = server_sql(spec, edits)
    abilities = client_skill_line_ability_table(skill_line_abilities(spec), stock)

    server_dbc = os.path.join(out, "dbc")
    client_dbc = os.path.join(out, "client", "DBFilesClient")
    sql_dir = os.path.join(out, "sql")
    for folder in (server_dbc, client_dbc, sql_dir):
        os.makedirs(folder, exist_ok=True)
    for folder in (server_dbc, client_dbc):
        talents.write(os.path.join(folder, "Talent.dbc"))
        stock.talent_tab.write(os.path.join(folder, "TalentTab.dbc"))
    spell_table.write(os.path.join(client_dbc, "Spell.dbc"))
    abilities.write(os.path.join(client_dbc, "SkillLineAbility.dbc"))
    with open(os.path.join(sql_dir, "forever_world.sql"), "w", encoding="utf-8", newline="\n") as file:
        file.write(sql)
    client_patch.write(os.path.join(out, "client"))
