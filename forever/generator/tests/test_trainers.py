"""The trainers of the new Blacksmithing recipes of jvbastel/WOWAI#239 (spec #225) in the generated data.

The rows of forever/spec/blacksmithing_trainers.tsv teach 8 new recipes at the trainers (forever_npc_trainer:
a copy of the stock trainer rows of one stock recipe, with the recipe's learn spell, skill and cost) and give
Copper Rod with the Blacksmithing skill (the AcquireMethod of its skill line ability, as stock Rough Copper
Vest 12260 has). The test runs the generator on the spec folder and reads what it wrote from the outside. The
expected values come from the ticket and from the stock world DB (npc_trainer of build 5875).
"""
import re
from pathlib import Path

import pytest

from test_blacksmithing import BLACKSMITHING, refused, sql_rows
from test_pipeline import SPEC_DIR, STOCK, generate, read_dbc

pytestmark = pytest.mark.skipif(not (STOCK / "Spell.dbc").is_file(), reason="no stock DBC folder at %s" % STOCK)

# The ticket's table: learn spell -> (recipe, Forever skill). The learn spells are those of the plans (#230).
BOOTS = {61197: (61000, 35), 61196: (61003, 35), 61198: (61004, 35)}
BELTS = {61257: (61078, 150), 61258: (61079, 150), 61259: (61080, 150), 61260: (61081, 150), 61261: (61082, 150)}
# The stock trainer rows that each copies (npc_trainer, build 5875): Copper Chain Belt (2661) is taught with
# learn spell 2744 at skill 35 for 90 copper by 20 trainers, Iron Buckle (8768) with 8769 at 150 for 225 copper
# by the 10 Journeyman and Expert trainers.
COPPER_CHAIN_BELT, IRON_BUCKLE = 2744, 8769
COPPER_ROD, ROUGH_COPPER_VEST = 61001, 12260
# SkillLineAbility.dbc 1.12.1: 2 Spell, 7 MinSkillRank, 9 AcquireMethod (1: learned with the profession skill).
SLA_SPELL, SLA_MIN_SKILL, SLA_ACQUIRE = 2, 7, 9


def sql(out):
    return (out / "sql" / "forever_world.sql").read_text(encoding="utf-8")


def trainer_copies(out):
    """{learn spell: (the stock learn spell whose trainer rows it copies, its columns)} of the generated SQL."""
    sources = re.findall(r"^CREATE TEMPORARY TABLE `forever_trainer_new` SELECT \* FROM `npc_trainer` "
                         r"WHERE `spell`=(\d+);$", sql(out), re.M)
    rows = sql_rows(out, "forever_trainer_new")
    assert len(sources) == len(rows)
    return {int(row["spell"]): (int(source), row) for source, row in zip(sources, rows)}


def test_the_boots_and_belts_are_taught_at_their_forever_skill_by_the_trainers_of_a_stock_recipe(out):
    copies = trainer_copies(out)
    assert set(copies) == set(BOOTS) | set(BELTS)
    for learn, (_, skill) in BOOTS.items():
        source, row = copies[learn]
        assert (source, row["reqskillvalue"], row["spellcost"]) == (COPPER_CHAIN_BELT, str(skill), "90"), learn
    for learn, (_, skill) in BELTS.items():
        source, row = copies[learn]
        assert (source, row["reqskillvalue"], row["spellcost"]) == (IRON_BUCKLE, str(skill), "225"), learn


def test_copper_rod_is_learned_with_the_skill_as_stock_rough_copper_vest_is(out):
    client, _ = read_dbc(out / "client" / "DBFilesClient" / "SkillLineAbility.dbc")
    stock, _ = read_dbc(STOCK / "SkillLineAbility.dbc")
    vest = next(row for row in stock.values() if row[SLA_SPELL] == ROUGH_COPPER_VEST and row[1] == BLACKSMITHING)
    rod = client[COPPER_ROD]
    assert (rod[1], rod[SLA_MIN_SKILL], rod[SLA_ACQUIRE]) == (vest[1], vest[SLA_MIN_SKILL], vest[SLA_ACQUIRE]) == (BLACKSMITHING, 1, 1)
    server = {int(row["id"]): row for row in sql_rows(out, "forever_sla_new")}
    assert (server[COPPER_ROD]["req_skill_value"], server[COPPER_ROD]["learn_on_get_skill"]) == ("1", "1")


def test_the_trainer_file_changes_nothing_else(out, tmp_path):
    # Stock trainer rows, plans items and the other recipes come out the same without the trainer file.
    spec = tmp_path / "spec"
    spec.mkdir()
    for path in SPEC_DIR.glob("*.tsv"):
        if path.name != "blacksmithing_trainers.tsv":
            (spec / path.name).write_bytes(path.read_bytes())
    before = generate(tmp_path / "before", spec)
    files = sorted(p.relative_to(out) for p in out.rglob("*") if p.is_file())
    assert files == sorted(p.relative_to(before) for p in before.rglob("*") if p.is_file())
    changed = {path for path in files if (out / path).read_bytes() != (before / path).read_bytes()}
    abilities = Path("client") / "DBFilesClient" / "SkillLineAbility.dbc"
    world = Path("sql") / "forever_world.sql"
    assert changed == {abilities, world, Path("client") / "client-data-patch.MPQ"}
    new, _ = read_dbc(out / abilities)
    old, _ = read_dbc(before / abilities)
    assert set(new) == set(old)
    assert {key for key in new if new[key] != old[key]} == {COPPER_ROD}
    assert new[COPPER_ROD][:SLA_ACQUIRE] + new[COPPER_ROD][SLA_ACQUIRE + 1:] == \
        old[COPPER_ROD][:SLA_ACQUIRE] + old[COPPER_ROD][SLA_ACQUIRE + 1:]
    # The SQL: the trainer copies are new lines, and Copper Rod's skill line ability learns it with the skill.
    new_lines = sql(out).splitlines()
    old_lines = sql(before).splitlines()
    rod = lambda line: "`id`=%d, " % COPPER_ROD in line
    kept = without_trainer_blocks(new_lines)
    assert len(new_lines) - len(kept) == 8 * 8  # 8 trainer rows, 8 lines each
    assert [line.replace("`learn_on_get_skill`=1", "`learn_on_get_skill`=0") if rod(line) else line for line in kept] == \
        old_lines


def without_trainer_blocks(lines):
    """The lines of the generated SQL without the blocks of the trainer rows: from the blank line before each
    trainer comment to its DROP line."""
    kept, inside = [], False
    for k, line in enumerate(lines):
        starts = k + 1 < len(lines) and line == "" and ": the trainers of stock recipe" in lines[k + 1]
        inside = inside or starts
        if not inside:
            kept.append(line)
        elif line == "DROP TEMPORARY TABLE `forever_trainer_new`;":
            inside = False
    return kept


def test_no_other_new_recipe_is_learned_with_the_skill(out):
    client, _ = read_dbc(out / "client" / "DBFilesClient" / "SkillLineAbility.dbc")
    stock, _ = read_dbc(STOCK / "SkillLineAbility.dbc")
    assert {key for key, row in client.items() if key not in stock and row[SLA_ACQUIRE]} == {COPPER_ROD}
    server = sql_rows(out, "forever_sla_new")
    assert {int(row["id"]) for row in server if row["learn_on_get_skill"] != "0"} == {COPPER_ROD}


def test_a_trainer_row_with_another_learn_spell_is_refused(tmp_path):
    error = refused(tmp_path, "blacksmithing_trainers.tsv", ("Strange Copper Boots\t61197\t", "Strange Copper Boots\t61196\t"))
    assert "the learn spell 61196 is not the learn spell of the recipe's plans (61197)" in error


def test_a_trainer_row_of_a_cut_or_unknown_recipe_is_refused(tmp_path):
    # Heavy Thorium Gauntlets (61191) needs a Master Forge: the dead-content rule cuts it.
    error = refused(tmp_path, "blacksmithing_trainers.tsv", ("trainer\t61000\tStrange Copper Boots\t", "trainer\t61191\tStrange Copper Boots\t"))
    assert "recipe 61191 is no new recipe of the spec, or the dead-content rule cuts it" in error


def test_trainers_from_a_stock_recipe_with_no_trainer_is_refused(tmp_path):
    # Rough Weightstone (3115) comes with the skill: no stock spell teaches it.
    error = refused(tmp_path, "blacksmithing_trainers.tsv", ("\t61197\t2661\t", "\t61197\t3115\t"))
    assert "the stock recipe 3115 has not one stock learn spell" in error


def test_an_auto_row_with_a_trainer_is_refused(tmp_path):
    error = refused(tmp_path, "blacksmithing_trainers.tsv", ("auto\t61001\tCopper Rod\t-\t-\t-\t", "auto\t61001\tCopper Rod\t-\t2661\t90\t"))
    assert "an auto row has no learnSpell, trainersFrom, cost (write -)" in error


def test_a_trainer_copy_fails_the_load_unless_trainers_teach_the_stock_recipe_at_or_above_the_skill(out):
    # A trainer template's rows would be missed by the copy (review L5 of #228), and a trainer whose rank caps
    # below the recipe's skill must not teach it.
    blocks = re.findall(r"^(.*)\nCREATE TEMPORARY TABLE `forever_trainer_new` SELECT \* FROM `npc_trainer` WHERE `spell`=(\d+);\n"
                        r"(.*)\nUPDATE `forever_trainer_new` SET `spell`=\d+, `reqskillvalue`=(\d+),", sql(out), re.M)
    assert len(blocks) == len(BOOTS) + len(BELTS)
    for template_guard, source, skill_guard, skill in blocks:
        assert template_guard == ("SELECT IF((SELECT COUNT(*) FROM `npc_trainer_template` WHERE `spell` IN (%s))=0, 0, "
                                  "(SELECT 1 UNION SELECT 2)) INTO @forever_found;" % source)
        assert skill_guard == ("SELECT IF((SELECT COUNT(*) > 0 AND MIN(`reqskillvalue`) >= %s FROM `forever_trainer_new`), 0, "
                               "(SELECT 1 UNION SELECT 2)) INTO @forever_found;" % skill)
