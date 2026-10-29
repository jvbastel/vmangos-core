"""Builds the Forever data from the spec and the stock 1.12.1 DBC files.

Three outputs, all from the same rows, so that the client and the server never drift apart:
  dbc/                  the Forever DBC folder of the server core (Forever.DataDir): the talent tables
  client/DBFilesClient/ the DBCs of the client data patch (#207 packs them into an MPQ)
  sql/forever_world.sql the Forever server tables (forever_spell_template, ...) of the world database
"""
import os

from .dbc import Dbc
from .spec import SpecError

# TalentTab.dbc IDs of the Warrior trees.
WARRIOR_TABS = {"arms": 161, "fury": 164, "protection": 163}

# Talent.dbc 1.12.1: 0 ID, 1 TabID, 2 Tier, 3 Column (from 0), 4-12 RankID[9], 13-15 DependsOn[3],
# 16-18 DependsOnRank[3] (0 = rank 1), 19 Flags, 20 DependsOnSpell.
TALENT_FIELDS = 21
# Spell.dbc 1.12.1 (see notes in forever/REFRESH.md): 76 EffectBasePoints[0], 117 SpellIconID,
# 120-127 Name[8], 129-136 Rank[8], 138-145 Description[8] (index 0 = enUS).
SPELL_ATTRIBUTES, SPELL_BASE_POINTS1, SPELL_ICON = 6, 76, 117
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


def new_spells(spec, stock):
    """(spell ID, source spell ID, talent, rank from 0) of each spec spell that is not a stock spell.
    A rank whose spell is its source spell keeps the stock row."""
    stock_spells = stock.spell.by_id()
    low, high = spec.id_range
    spells = []
    for talent in spec.talents:
        for rank, (spell, source) in enumerate(zip(talent.spells, talent.source_spells)):
            if source not in stock_spells:
                raise SpecError("talent %d rank %d: source spell %d is no stock spell" % (talent.talent_id, rank + 1, source))
            if spell == source:
                continue
            if spell in stock_spells:
                raise SpecError("talent %d rank %d: spell %d clashes with a stock spell" % (talent.talent_id, rank + 1, spell))
            if not low <= spell <= high:
                raise SpecError("talent %d rank %d: spell %d is outside the free ID range %d-%d"
                                % (talent.talent_id, rank + 1, spell, low, high))
            spells.append((spell, source, talent, rank))
    return sorted(spells, key=lambda s: s[0])


def talent_table(spec, stock):
    """Talent.dbc: the stock rows of the other classes in their order, then the spec rows by ID."""
    stock_talents = stock.talent.by_id()
    stock_spells = stock.spell.by_id()
    low, high = spec.id_range
    rows = [list(row) for row in stock.talent.rows if row[1] not in WARRIOR_TABS.values()]
    for talent in sorted(spec.talents, key=lambda t: t.talent_id):
        old = stock_talents.get(talent.talent_id)
        if old is None and not low <= talent.talent_id <= high:
            raise SpecError("talent %d is no 1.12 talent and is outside the free ID range %d-%d"
                            % (talent.talent_id, low, high))
        if old is not None and old[1] not in WARRIOR_TABS.values():
            raise SpecError("talent %d clashes with a stock talent of another class" % talent.talent_id)
        # Flags 1 marks a talent that teaches an active ability. A kept talent keeps its stock value.
        flags = old[19] if old else int(not stock_spells[talent.source_spells[0]][SPELL_ATTRIBUTES] & SPELL_ATTR_PASSIVE)
        row = [0] * TALENT_FIELDS
        row[0] = talent.talent_id
        row[1] = WARRIOR_TABS[talent.tree]
        row[2] = talent.tier - 1
        row[3] = talent.column - 1
        row[4:4 + talent.ranks] = talent.spells
        row[13] = talent.required_talent
        row[16] = talent.required_rank - 1 if talent.required_talent else 0
        row[19] = flags
        rows.append(row)
    return Dbc(stock.talent.fields, rows, bytes(stock.talent.strings))


def client_spell_table(spells, stock):
    """Spell.dbc: every stock row as it is, plus the new spell rows, sorted by ID."""
    table = stock.spell.copy()
    by_id = stock.spell.by_id()
    for spell, source, talent, rank in spells:
        row = list(by_id[source])
        row[0] = spell
        row[SPELL_BASE_POINTS1] = talent.effect1[rank] - 1
        row[SPELL_ICON] = talent.icon
        for first, value in ((SPELL_NAME, talent.name), (SPELL_RANK, "Rank %d" % (rank + 1)),
                             (SPELL_DESCRIPTION, talent.tooltip)):
            # enUS only: the other locale strings of the stock row would be stale.
            row[first:first + 8] = [table.add_text(value)] + [0] * 7
        table.rows.append(row)
    table.rows.sort(key=lambda r: r[0])
    return table


def _sql_text(value):
    return "'" + value.replace("\\", "\\\\").replace("'", "\\'") + "'"


def server_sql(spells):
    """The Forever server tables. Each new spell is a copy of the stock server row of its source spell
    (with the core's own fixes), with the spec values set on top."""
    lines = ["-- The Forever server tables of the world database. Generated by forever/generator from",
             "-- forever/warrior.tsv: never edit by hand. The core reads them only with Forever.Rules = 1.", ""]
    lines += ["DELETE FROM `%s`;" % table for table in FOREVER_TABLES]
    for spell, source, talent, rank in spells:
        values = [("entry", str(spell)), ("build", str(CLIENT_BUILD)), ("name", _sql_text(talent.name)),
                  ("nameSubtext", _sql_text("Rank %d" % (rank + 1))), ("description", _sql_text(talent.tooltip)),
                  ("spellIconId", str(talent.icon)), ("effectBasePoints1", str(talent.effect1[rank] - 1))]
        lines += [
            "",
            "-- %s rank %d (talent %d), from spell %d." % (talent.name, rank + 1, talent.talent_id, source),
            "CREATE TEMPORARY TABLE `forever_spell_new` SELECT * FROM `spell_template` WHERE `entry`=%d "
            "AND `build`<=%d ORDER BY `build` DESC LIMIT 1;" % (source, CLIENT_BUILD),
            "UPDATE `forever_spell_new` SET %s;" % ", ".join("`%s`=%s" % kv for kv in values),
            "INSERT INTO `forever_spell_template` SELECT * FROM `forever_spell_new`;",
            "DROP TEMPORARY TABLE `forever_spell_new`;",
        ]
    return "\n".join(lines) + "\n"


def generate(spec, stock_folder, out):
    """Checks the spec against the stock data, then writes the three outputs into `out`."""
    stock = Stock(stock_folder)
    spells = new_spells(spec, stock)
    talents = talent_table(spec, stock)
    spell_table = client_spell_table(spells, stock)
    sql = server_sql(spells)

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
