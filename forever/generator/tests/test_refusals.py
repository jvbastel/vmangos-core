"""The generator refuses a spec that would break the realm (spec jvbastel/WOWAI#204, testing decision 1,
negative cases). Each test changes one thing in the real spec file and runs the generator as the test realm
kit does: it must stop with an error that names the broken rule, and write nothing.
"""
import subprocess
import sys

import pytest

from test_pipeline import GENERATOR, SPEC, STOCK, pytestmark  # noqa: F401  (pytestmark: skip without stock data)


def spec_lines():
    return SPEC.read_text(encoding="utf-8").splitlines()


def change_row(lines, name, **changes):
    """The spec lines with the columns `changes` set on the row named `name`."""
    header = next(line for line in lines if line and not line.startswith("#")).split("\t")
    result, found = [], False
    for line in lines:
        values = line.split("\t")
        if not line.startswith("#") and len(values) == len(header) and values[header.index("name")] == name:
            for column, value in changes.items():
                values[header.index(column)] = value
            line, found = "\t".join(values), True
        result.append(line)
    assert found, name
    return result


def talent_line(talent_id, tier, column, spell, tree="arms", name="Extra Talent"):
    return "\t".join(["talent", str(talent_id), tree, str(tier), str(column), "1", "0", "0", name, "1721", "data",
                      str(spell), "16462", "-", "-", "-", "effectApplyAuraName1=4", "Text.", ""])


def refused(tmp_path, lines):
    """The error of the generator on a spec of these lines. The generator must fail and write nothing."""
    spec = tmp_path / "spec.tsv"
    spec.write_text("\n".join(lines) + "\n", encoding="utf-8")
    out = tmp_path / "out"
    done = subprocess.run([sys.executable, "-I", str(GENERATOR), "--spec", str(spec), "--stock", str(STOCK),
                           "--out", str(out)], capture_output=True, text=True)
    assert done.returncode != 0, done.stdout
    assert not out.exists() or not any(out.rglob("*")), "the generator wrote output"
    assert "Traceback" not in done.stderr, done.stderr
    return done.stderr


def test_the_real_spec_is_accepted(tmp_path):
    spec = tmp_path / "spec.tsv"
    spec.write_text("\n".join(spec_lines()) + "\n", encoding="utf-8")
    subprocess.run([sys.executable, "-I", str(GENERATOR), "--spec", str(spec), "--stock", str(STOCK),
                    "--out", str(tmp_path / "out")], check=True)


def test_more_than_7_tiers(tmp_path):
    assert "tier 8" in refused(tmp_path, change_row(spec_lines(), "Mortal Strike", tier="8"))


def test_more_than_4_columns(tmp_path):
    assert "column 5" in refused(tmp_path, change_row(spec_lines(), "Deflection", column="5"))


def test_more_than_20_talents_in_a_tree(tmp_path):
    # Arms has 17 talents. Four more on free cells make 21.
    extra = [talent_line(60100 + i, tier, column, 60200 + i)
             for i, (tier, column) in enumerate([(1, 4), (2, 3), (3, 1), (3, 4)])]
    assert "more than 20 talents per tree" in refused(tmp_path, spec_lines() + extra)


def test_more_than_5_ranks(tmp_path):
    lines = change_row(spec_lines(), "Bloodthrill", ranks="6", spells="60101,60102,60103,60104,60105,60199",
                       sourceSpells="16462,16462,16462,16462,16462,16462", effect1="4/8/12/16/20/24")
    assert "6 ranks, at most 5 ranks" in refused(tmp_path, lines)


def test_more_than_one_prerequisite(tmp_path):
    lines = change_row(spec_lines(), "Mortal Strike", requiredTalent="133,121")
    assert "at most one prerequisite" in refused(tmp_path, lines)


def test_two_talents_on_one_cell(tmp_path):
    assert "is also talent" in refused(tmp_path, change_row(spec_lines(), "Deflection", column="1"))


def test_a_talent_id_of_another_class(tmp_path):
    # Talent 23 is a stock Mage talent.
    lines = change_row(spec_lines(), "Bloodthrill", talentId="23")
    assert "clashes with a stock talent of another class" in refused(tmp_path, lines)


def test_a_new_spell_with_a_stock_spell_id(tmp_path):
    # Spell 133 is the stock Fireball.
    lines = change_row(spec_lines(), "Victory Rush", spells="133")
    assert "clashes with a stock spell" in refused(tmp_path, lines)


def test_a_talent_that_takes_the_stock_spell_of_another_talent(tmp_path):
    # 12700 is rank 1 of the stock Axe Specialization (dropped in Forever), not of Raging Blows.
    lines = change_row(spec_lines(), "Raging Blows", spells="12700", sourceSpells="12700")
    assert "no rank of this talent" in refused(tmp_path, lines)


def test_one_new_id_twice(tmp_path):
    lines = change_row(spec_lines(), "Victorious", spells="60123")
    assert "same ID" in refused(tmp_path, lines)


def test_a_new_id_outside_the_free_range(tmp_path):
    lines = change_row(spec_lines(), "Victory Rush", spells="40000")
    assert "outside the free ID range" in refused(tmp_path, lines)


@pytest.mark.parametrize("icon", ["108", "456"])  # Flurry and Battle Shout markers of the core
def test_an_icon_that_the_core_uses_as_a_spell_marker(tmp_path, icon):
    lines = change_row(spec_lines(), "Bloodthrill", icon=icon)
    assert "spell marker" in refused(tmp_path, lines)


def test_a_marker_icon_on_a_changed_stock_spell(tmp_path):
    # Improved Rend keeps its stock spells; giving them a marker icon is a new marker on them.
    lines = change_row(spec_lines(), "Improved Rend", icon="108")
    assert "spell marker" in refused(tmp_path, lines)


def test_an_icon_that_is_no_stock_icon(tmp_path):
    lines = change_row(spec_lines(), "Bloodthrill", icon="99999")
    assert "no stock SpellIcon.dbc ID" in refused(tmp_path, lines)


def test_a_skill_line_on_a_stock_spell(tmp_path):
    # A skill line ability takes the ID of its spell: only a new spell's ID is free in SkillLineAbility.dbc.
    error = refused(tmp_path, change_row(spec_lines(), "Berserker Rage", set="skillLine=256"))
    assert "skill line" in error and "18499" in error


def test_a_recipe_that_the_dead_content_rule_cuts_still_gets_the_id_checks(tmp_path):
    # Review L2: Enriched Thorium Breastplate needs a Master Forge, so the rule cuts it; its spell ID must
    # still be in the profession range, or the bad ID comes back when a refresh keeps the recipe.
    from test_blacksmithing import refused
    error = refused(tmp_path, "blacksmithing.tsv", ("recipe\t61117\t16731\t", "recipe\t60500\t16731\t"))
    assert "recipe Enriched Thorium Breastplate: spell 60500 is outside the free profession spell ID range" in error
