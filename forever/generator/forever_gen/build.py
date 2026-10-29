"""Builds the Forever data from the spec and the stock 1.12.1 DBC files.

Three outputs, all from the same rows, so that the client and the server never drift apart:
  dbc/                  the Forever DBC folder of the server core (Forever.DataDir): the talent tables
  client/DBFilesClient/ the DBCs of the client data patch
  client/client-data-patch.MPQ  the client data patch: those DBCs in one MPQ
  sql/forever_world.sql the Forever server tables (forever_spell_template, ...) of the world database
"""
import os

from . import client_patch, rules
from .dbc import Dbc
from .spec import FIELDS, NAME_SUBTEXT, talents

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

# The world database build of the Forever rows (SUPPORTED_CLIENT_BUILD of the core).
CLIENT_BUILD = 5875
FOREVER_TABLES = ("forever_spell_template", "forever_spell_chain", "forever_spell_proc_event",
                  "forever_skill_line_ability")


class Stock:
    """The stock DBC files that the generator reads."""

    def __init__(self, folder):
        self.talent = Dbc.read(os.path.join(folder, "Talent.dbc"))
        self.talent_tab = Dbc.read(os.path.join(folder, "TalentTab.dbc"))
        self.spell = Dbc.read(os.path.join(folder, "Spell.dbc"))
        self.spell_icon = Dbc.read(os.path.join(folder, "SpellIcon.dbc"))


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
    changes += [(name, values[rank]) for name, values in row.fields.items() if name != NAME_SUBTEXT]
    return changes


def spell_texts(row, rank, stock_rank_text):
    """(name, rank text, description or None to keep the stock one) of one rank."""
    return row.name, _rank_text(row, rank, stock_rank_text), row.tooltip


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


def client_spell_table(spells, stock):
    """Spell.dbc: every stock row, the spec changes on the stock spells that the spec names, and the new
    spell rows, sorted by ID. A stock row whose values the spec does not change stays byte for byte."""
    table = stock.spell.copy()
    by_id = table.by_id()
    stock_by_id = stock.spell.by_id()
    for spell, source, row, rank in spells:
        new = spell not in stock_by_id
        target = list(stock_by_id[source]) if new else by_id[spell]
        target[0] = spell
        for name, value in spell_changes(row, rank):
            target[FIELDS[name] if name != "spellIconId" else SPELL_ICON] = value & 0xFFFFFFFF
        stock_rank_text = "" if new else table.text(target[SPELL_RANK])
        for first, value in zip((SPELL_NAME, SPELL_RANK, SPELL_DESCRIPTION), spell_texts(row, rank, stock_rank_text)):
            # enUS only: the other locale strings of the stock row would be stale.
            if value is not None and (new or table.text(target[first]) != value):
                target[first:first + 8] = [table.add_text(value)] + [0] * 7
        if new:
            table.rows.append(target)
    table.rows.sort(key=lambda r: r[0])
    return table


def _sql_text(value):
    return "'" + value.replace("\\", "\\\\").replace("'", "\\'") + "'"


def server_sql(spells, stock):
    """The Forever server tables. Each spec spell is a copy of the stock server row of its source spell
    (with the core's own fixes), with the spec values set on top. The tables are made again in the shape
    of the stock tables first, so that a new column of a stock table cannot break the copies."""
    lines = ["-- The Forever server tables of the world database. Generated by forever/generator from",
             "-- forever/warrior.tsv: never edit by hand. The core reads them only with Forever.Rules = 1.", ""]
    for table in FOREVER_TABLES:
        lines += ["DROP TABLE IF EXISTS `%s`;" % table,
                  "CREATE TABLE `%s` LIKE `%s`;" % (table, table[len("forever_"):])]
    stock_by_id = stock.spell.by_id()
    for spell, source, row, rank in spells:
        stock_rank_text = stock.spell.text(stock_by_id[spell][SPELL_RANK]) if spell in stock_by_id else ""
        name, rank_text, description = spell_texts(row, rank, stock_rank_text)
        values = [("entry", str(spell)), ("build", str(CLIENT_BUILD)), ("name", _sql_text(name)),
                  ("nameSubtext", _sql_text(rank_text))]
        if description is not None:
            values.append(("description", _sql_text(description)))
        values += [(column, str(value)) for column, value in spell_changes(row, rank)]
        lines += [
            "",
            "-- %s rank %d, %s." % (row.name, rank + 1,
                                    "from spell %d" % source if spell != source else "changes the stock spell"),
            "CREATE TEMPORARY TABLE `forever_spell_new` SELECT * FROM `spell_template` WHERE `entry`=%d "
            "AND `build`<=%d ORDER BY `build` DESC LIMIT 1;" % (source, CLIENT_BUILD),
            "UPDATE `forever_spell_new` SET %s;" % ", ".join("`%s`=%s" % kv for kv in values),
            "INSERT INTO `forever_spell_template` SELECT * FROM `forever_spell_new`;",
            "DROP TEMPORARY TABLE `forever_spell_new`;",
        ]
    return "\n".join(lines) + "\n"


def generate(spec, stock_folder, out, marker_icons):
    """Checks the spec against the stock data and the core's marker icons, then writes the three outputs
    into `out`. A spec that breaks a rule raises a SpecError before anything is written."""
    stock = Stock(stock_folder)
    rules.check(spec, stock, marker_icons)
    spells = spell_ranks(spec)
    talents = in_client_order(talent_table(spec, stock))
    spell_table = client_spell_table(spells, stock)
    sql = server_sql(spells, stock)

    server_dbc = os.path.join(out, "dbc")
    client_dbc = os.path.join(out, "client", "DBFilesClient")
    sql_dir = os.path.join(out, "sql")
    for folder in (server_dbc, client_dbc, sql_dir):
        os.makedirs(folder, exist_ok=True)
    for folder in (server_dbc, client_dbc):
        talents.write(os.path.join(folder, "Talent.dbc"))
        stock.talent_tab.write(os.path.join(folder, "TalentTab.dbc"))
    spell_table.write(os.path.join(client_dbc, "Spell.dbc"))
    with open(os.path.join(sql_dir, "forever_world.sql"), "w", encoding="utf-8", newline="\n") as file:
        file.write(sql)
    client_patch.write(os.path.join(out, "client"))
