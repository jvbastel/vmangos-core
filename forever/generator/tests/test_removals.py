"""The removed stock recipes of jvbastel/WOWAI#229 (spec #225) and the removal marker in the generated data.

A `removed` row of forever/spec/blacksmithing.tsv takes a stock recipe away on the Forever realm: its stock
skill line ability leaves the client SkillLineAbility.dbc, and the server gets a copy of each stock row of it
(forever_skill_line_ability by ID, forever_npc_trainer by learn spell) with the removal marker set. The core
then loads neither the stock row nor the marked copy. The spell itself stays: a character who knows it keeps
it (#233). The test runs the generator on the spec folder and reads what it wrote from the outside.
"""
import re

import pytest

from test_blacksmithing import BLACKSMITHING, refused as refused_in, spec_rows
from test_pipeline import ROOT, STOCK, generate, read_dbc

pytestmark = pytest.mark.skipif(not (STOCK / "SkillLineAbility.dbc").is_file(), reason="no stock DBC folder at %s" % STOCK)

# The six recipes that Forever removed (ticket #229, notes/forever/explore-professions.md gotcha 8): spell,
# its stock SkillLineAbility ID, its learn spell (the spell of the plans) and its plans item.
REMOVED = {"Wildthorn Mail": (16650, 9167, 16679, 12691), "Helm of the Great Chief": (16728, 9325, 16750, 12716),
           "Invulnerable Mail": (16746, 9338, 16762, 12728), "Storm Gauntlets": (16661, 9579, 16692, 12703),
           "Heavy Timbermaw Belt": (23628, 12677, 23630, 19202), "Heavy Timbermaw Boots": (23629, 12678, 23631, 19204)}
MARKER = "forever_removed"
SLA_SPELL = 2


def removal_rows():
    return [row for row in spec_rows("blacksmithing.tsv") if row["kind"] == "removed"]


@pytest.fixture(scope="module")
def out(tmp_path_factory):
    return generate(tmp_path_factory.mktemp("forever229"))


def sql(out):
    return (out / "sql" / "forever_world.sql").read_text(encoding="utf-8")


def test_the_spec_has_a_removal_row_for_each_of_the_six_recipes():
    rows = {row["name"]: row for row in removal_rows()}
    assert set(rows) == set(REMOVED)
    for name, (spell, _, _, plans) in REMOVED.items():
        assert (int(rows[name]["spell"]), int(rows[name]["sourceSpell"]), rows[name]["skillLine"]) == \
            (spell, spell, str(BLACKSMITHING)), name
        assert rows[name]["plans"] == str(plans), name


def test_a_removed_recipe_leaves_the_client_skill_line_and_nothing_else_does(out):
    client, _ = read_dbc(out / "client" / "DBFilesClient" / "SkillLineAbility.dbc")
    stock, _ = read_dbc(STOCK / "SkillLineAbility.dbc")
    removed_spells = {spell for spell, _, _, _ in REMOVED.values()}
    assert not [row for row in client.values() if row[SLA_SPELL] in removed_spells]
    assert set(stock) - set(client) == {sla for _, sla, _, _ in REMOVED.values()}


def test_the_removed_recipe_spells_stay_in_the_client_spell_table(out):
    # A character who knows one keeps it (#233); the craft window no longer lists it.
    client, _ = read_dbc(out / "client" / "DBFilesClient" / "Spell.dbc")
    assert all(spell in client for spell, _, _, _ in REMOVED.values())


def marked(out, table):
    """{key column: set of keys} of the removal lines of forever_<table> in the generated SQL."""
    found = re.findall(r"^INSERT INTO `forever_%s` SELECT \*, 1 FROM `%s` WHERE `(\w+)` IN \(([\d, ]+)\)" % (table, table),
                       sql(out), re.M)
    keys = {}
    for column, values in found:
        keys.setdefault(column, set()).update(int(v) for v in values.split(", "))
    return keys


def test_each_removal_row_marks_its_stock_skill_line_ability_on_the_server(out):
    assert marked(out, "skill_line_ability") == {"id": {sla for _, sla, _, _ in REMOVED.values()}}


def test_each_removal_row_marks_the_trainer_rows_of_its_learn_spell_on_the_server(out):
    # None of the six has a stock trainer row (they come from plans), so the copy holds no row today. The
    # marker still covers any trainer row that teaches the learn spell.
    assert marked(out, "npc_trainer") == {"spell": {learn for _, _, learn, _ in REMOVED.values()}}


def test_every_override_table_gets_the_marker_after_all_its_other_rows(out):
    text = sql(out)
    tables = re.findall(r"^CREATE TABLE `(forever_\w+)` LIKE `\w+`;$", text, re.M)
    assert {"forever_skill_line_ability", "forever_npc_trainer", "forever_item_template",
            "forever_spell_template"} <= set(tables)
    for table in tables:
        alter = "ALTER TABLE `%s` ADD COLUMN `%s` TINYINT UNSIGNED NOT NULL DEFAULT 0;" % (table, MARKER)
        assert text.count(alter) == 1, table
        after = text[text.index(alter):]
        # A row copied with SELECT * after the marker column has one column too few: the load would fail.
        assert not re.search(r"INSERT INTO `%s` SELECT \* FROM" % table, after), table


def test_the_core_reads_the_marker_of_the_generator():
    core = (ROOT / "src" / "game" / "Forever" / "Forever.cpp").read_text(encoding="utf-8")
    assert 'RemovalMarker = "%s"' % MARKER in core


def refused(tmp_path, *changes):
    return refused_in(tmp_path, "blacksmithing.tsv", *changes)


BELT = "removed\t23628\t23628\tHeavy Timbermaw Belt\t164\t"


def test_a_removal_row_that_is_no_stock_recipe_of_its_skill_line_is_refused(tmp_path):
    error = refused(tmp_path, (BELT, BELT.replace("\t164\t", "\t186\t")))
    assert "spell 23628 has no stock skill line ability in skill line 186" in error


def test_a_removal_row_of_a_recipe_that_the_spec_also_changes_is_refused(tmp_path):
    # Runed Copper Breastplate (2667) is a stock row of #228.
    error = refused(tmp_path, (BELT, "removed\t2667\t2667\tRuned Copper Breastplate\t164\t"))
    assert "spell 2667 is also a recipe row of the spec" in error


def test_a_removal_row_with_another_source_spell_is_refused(tmp_path):
    error = refused(tmp_path, (BELT, BELT.replace("23628\t23628", "23628\t23629")))
    assert "a removal row names the stock spell twice (sourceSpell 23629)" in error
