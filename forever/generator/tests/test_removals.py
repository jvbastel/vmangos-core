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
# The override tables whose stock rows cannot be removed: they get no marker column.
UNMARKED = {"forever_spell_template"}
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
    # No trainer template teaches it either: that table has no Forever override, so the SQL fails (review L5).
    for _, _, learn, _ in REMOVED.values():
        assert "FROM `npc_trainer_template` WHERE `spell` IN (%d))=0" % learn in sql(out)


def plans_copy(out, plans):
    """The SQL lines that copy the stock plans `plans` into forever_item_template, or []."""
    lines = sql(out).splitlines()
    start = "CREATE TEMPORARY TABLE `forever_plans_gone` SELECT * FROM `item_template` WHERE `entry`=%d " % plans
    for k, line in enumerate(lines):
        if line.startswith(start):
            end = lines.index("DROP TEMPORARY TABLE `forever_plans_gone`;", k)
            return lines[k:end + 1]
    return []


def test_the_plans_of_each_removed_recipe_teach_nothing_on_the_server(out):
    # The six come from plans (world drops, Timbermaw vendors). On the Forever realm the plans stay an item
    # (a character who holds them keeps them), but their copy has no learn spell: they cannot be used.
    for name, (_, _, learn, plans) in REMOVED.items():
        block = plans_copy(out, plans)
        assert block, name
        assert "INSERT INTO `forever_item_template` SELECT * FROM `forever_plans_gone`;" in block, name
        # The copy keeps the content patch of the stock plans (Timbermaw plans: a later patch).
        assert not [line for line in block if "`patch`=" in line], name
        for k in range(1, 6):
            emptied = [line for line in block if line.startswith("UPDATE `forever_plans_gone` SET `spellid_%d`=0, "
                                                                 "`spelltrigger_%d`=0" % (k, k))]
            assert emptied and emptied[0].endswith("WHERE `spellid_%d` IN (%d);" % (k, learn)), (name, k)


def test_a_plans_copy_fails_the_load_when_the_plans_do_not_teach_the_recipe(out):
    # Without the learn spell in a slot of the stock plans row, the SQL fails instead of copying a row that
    # still teaches something else.
    for name, (_, _, learn, plans) in REMOVED.items():
        guard = [line for line in plans_copy(out, plans) if line.startswith("SELECT IF(")]
        assert guard and all(str(learn) in line for line in guard[:1]), name


def test_every_override_table_gets_the_marker_after_all_its_other_rows(out):
    text = sql(out)
    tables = re.findall(r"^CREATE TABLE `(forever_\w+)` LIKE `\w+`;$", text, re.M)
    assert {"forever_skill_line_ability", "forever_npc_trainer", "forever_item_template",
            "forever_spell_template"} <= set(tables)
    for table in set(tables) - UNMARKED:
        alter = "ALTER TABLE `%s` ADD COLUMN `%s` TINYINT UNSIGNED NOT NULL DEFAULT 0;" % (table, MARKER)
        assert text.count(alter) == 1, table
        after = text[text.index(alter):]
        # A row copied with SELECT * after the marker column has one column too few: the load would fail.
        assert not re.search(r"INSERT INTO `%s` SELECT \* FROM" % table, after), table


def test_forever_spell_template_gets_no_marker(out):
    # The core reads forever_spell_template by position with SELECT * (Forever::LoadSpellTemplate), not by
    # Table(), and a spell cannot be removed (removals.NOT_REMOVABLE): a marker there would not be honoured.
    assert "ALTER TABLE `forever_spell_template` ADD COLUMN" not in sql(out)


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


def test_a_removal_row_whose_plans_are_an_item_row_of_the_spec_is_refused(tmp_path):
    # 3609 (Plans: Copper Chain Vest) is the source item of new plans rows, not an item row; 2844 (Copper
    # Mace) is a changed stock item row of items.tsv.
    error = refused(tmp_path, (BELT + "-\t-\t-\t-\t-\t-\t19202", BELT + "-\t-\t-\t-\t-\t-\t2844"))
    assert "removed recipe Heavy Timbermaw Belt: spell 23628: its plans 2844 are also an item row of the spec" in error
