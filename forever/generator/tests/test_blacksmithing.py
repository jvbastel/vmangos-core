"""The Forever Blacksmithing data of jvbastel/WOWAI#226 (spec #225, testing decision 1) in the generated data.

It runs the generator on the spec folder and reads what it wrote from the outside: the client Spell.dbc and
SkillLineAbility.dbc of the client data patch, and the server SQL. The recipe rows are in
forever/spec/blacksmithing.tsv, the item rows in forever/spec/items.tsv.
"""
import re

import pytest

from test_pipeline import SPEC_DIR, STOCK, generate, int32, read_dbc

pytestmark = pytest.mark.skipif(not (STOCK / "Spell.dbc").is_file(), reason="no stock DBC folder at %s" % STOCK)

# Spell.dbc 1.12.1: 15 RequiresSpellFocus, 18 CastingTimeIndex, 40-41 Totem, 42-49 Reagent, 50-57 ReagentCount,
# 61 Effect1, 64 EffectDieSides1, 76 EffectBasePoints1, 103 EffectItemType1, 117 SpellIconID, 120 Name (enUS).
FOCUS, CAST_TIME, TOTEMS, REAGENTS, REAGENT_COUNTS = 15, 18, slice(40, 42), slice(42, 50), slice(50, 58)
EFFECT1, DIE_SIDES1, BASE_POINTS1, ITEM_TYPE1, ICON, NAME = 61, 64, 76, 103, 117, 120
CREATE_ITEM = 24
# SkillLineAbility.dbc 1.12.1: 0 ID, 1 SkillLine, 2 Spell, 3 RaceMask, 4 ClassMask, 7 MinSkillRank,
# 9 AcquireMethod, 10 grey (TrivialSkillLineRankHigh), 11 yellow (TrivialSkillLineRankLow).
BLACKSMITHING = 164
# item_template stat types: 7 Stamina. Spell trigger 1: on equip.
STAMINA, ON_EQUIP = 7, 1


def spec_rows(name):
    lines = [line for line in (SPEC_DIR / name).read_text(encoding="utf-8").splitlines()
             if line and not line.startswith("#")]
    header = lines[0].split("\t")
    return [dict(zip(header, line.split("\t"))) for line in lines[1:]]


def cut_ids(out, kind):
    """The IDs of the rows of this kind that the dead-content rule cut (#230): the cut list of the output."""
    if out is None:
        return set()
    lines = (out / "cut-list.tsv").read_text(encoding="utf-8").splitlines()[2:]
    return {int(line.split("\t")[1]) for line in lines if line.split("\t")[0] == kind}


def new_recipes(out=None):
    """The new recipe rows of blacksmithing.tsv (kind recipe; the stock rows of #228 change stock recipes),
    without the ones that the generator of `out` cut."""
    cut = cut_ids(out, "recipe")
    return [row for row in spec_rows("blacksmithing.tsv") if row["kind"] == "recipe" and int(row["spell"]) not in cut]


def new_items(out=None):
    """The new item rows of items.tsv (kind item; the changed stock items of #227 are kind stock), without the
    ones that the generator of `out` cut."""
    cut = cut_ids(out, "item")
    return [row for row in spec_rows("items.tsv") if row["kind"] == "item" and int(row["entry"]) not in cut]


def counted(text):
    """[(item, count)] of a spec value "item x count, ..."."""
    return [tuple(int(v) for v in part.split("x")) for part in text.split(",")]


def free_range(name, label):
    m = re.search(r"^# Free %s range: (\d+)-(\d+)\." % label, (SPEC_DIR / name).read_text(encoding="utf-8"), re.M)
    return int(m.group(1)), int(m.group(2))


def unquote(value):
    """The text of a MySQL value: a string literal without its quotes and escapes, or a number."""
    if not value.startswith("'"):
        return value
    return re.sub(r"\\(.)", r"\1", value[1:-1])


def sql_rows(out, temp_table):
    """{key: {column: value}} of the UPDATE lines on a temporary copy table of the generated SQL."""
    sql = (out / "sql" / "forever_world.sql").read_text(encoding="utf-8")
    rows = []
    for m in re.finditer(r"^UPDATE `%s` SET (.*);$" % temp_table, sql, re.M):
        values = dict(re.findall(r"`(\w+)`=('(?:[^'\\]|\\.)*'|-?\d+(?:\.\d+)?)", m.group(1)))
        rows.append({k: unquote(v) for k, v in values.items()})
    return rows


@pytest.fixture(scope="module")
def out(tmp_path_factory):
    return generate(tmp_path_factory.mktemp("forever226"))


def test_the_spec_folder_has_the_warrior_blacksmithing_and_item_files():
    assert {"warrior.tsv", "blacksmithing.tsv", "items.tsv"} <= {p.name for p in SPEC_DIR.glob("*.tsv")}
    for name in ("blacksmithing.tsv", "items.tsv"):
        text = (SPEC_DIR / name).read_text(encoding="utf-8")
        assert "build 1.60.1.70685, recorded 2026-10-30" in text, name
    assert spec_rows("blacksmithing.tsv") and spec_rows("items.tsv")


def test_each_recipe_is_a_client_spell_that_makes_its_item_at_its_focus_from_its_reagents(out):
    client, text = read_dbc(out / "client" / "DBFilesClient" / "Spell.dbc")
    stock, _ = read_dbc(STOCK / "Spell.dbc")
    foci, focus_text = read_dbc(STOCK / "SpellFocusObject.dbc")
    focus_ids = {focus_text(row[1]): key for key, row in foci.items()}
    for recipe in new_recipes(out):
        spell, source = int(recipe["spell"]), int(recipe["sourceSpell"])
        row = client[spell]
        reagents = counted(recipe["reagents"])
        (item, count), = counted(recipe["creates"])
        assert text(row[NAME]) == recipe["name"], spell
        assert list(row[REAGENTS]) == [r for r, _ in reagents] + [0] * (8 - len(reagents)), spell
        assert list(row[REAGENT_COUNTS]) == [c for _, c in reagents] + [0] * (8 - len(reagents)), spell
        assert (row[EFFECT1], row[ITEM_TYPE1], int32(row[BASE_POINTS1]) + row[DIE_SIDES1]) == (CREATE_ITEM, item, count)
        assert row[FOCUS] == focus_ids[recipe["focus"]], spell
        # The tool, cast time and icon of the stock recipe that it copies.
        assert (row[TOTEMS], row[CAST_TIME], row[ICON]) == (stock[source][TOTEMS], stock[source][CAST_TIME], stock[source][ICON])


def test_the_tracer_recipe_is_strange_copper_boots_at_an_anvil(out):
    # Worked example from the Forever client (fv_new_recipes.tsv, spell 1252230): Light Leather x2, Copper
    # Bar x2, Strange Dust x4 at an Anvil (SpellFocusObject 1), with the Blacksmith Hammer (5956).
    client, text = read_dbc(out / "client" / "DBFilesClient" / "Spell.dbc")
    row = client[61000]
    assert text(row[NAME]) == "Strange Copper Boots"
    assert row[REAGENTS][:3] == (2318, 2840, 10940) and row[REAGENT_COUNTS][:3] == (2, 2, 4)
    assert (row[FOCUS], row[TOTEMS][0], row[ITEM_TYPE1]) == (1, 5956, 70000)


def test_each_recipe_has_its_skill_line_and_colour_range_in_the_client(out):
    abilities, _ = read_dbc(out / "client" / "DBFilesClient" / "SkillLineAbility.dbc")
    by_spell = {row[2]: row for row in abilities.values()}
    for recipe in new_recipes(out):
        row = by_spell[int(recipe["spell"])]
        assert (row[1], row[3], row[4]) == (int(recipe["skillLine"]), 0, 0)
        assert (row[7], row[9], row[11], row[10]) == (int(recipe["minRank"]), 0, int(recipe["yellow"]), int(recipe["grey"]))
    assert (by_spell[61000][1], by_spell[61000][11], by_spell[61000][10]) == (BLACKSMITHING, 40, 40)


def test_each_recipe_is_in_the_server_spell_and_skill_line_tables(out):
    spells = {int(row["entry"]): row for row in sql_rows(out, "forever_spell_new")}
    abilities = {int(row["spell_id"]): row for row in sql_rows(out, "forever_sla_new")}
    for recipe in new_recipes(out):
        spell = int(recipe["spell"])
        server = spells[spell]
        reagents = counted(recipe["reagents"])
        (item, count), = counted(recipe["creates"])
        assert server["name"] == recipe["name"]
        assert [int(server["reagent%d" % k]) for k in range(1, 9)] == [r for r, _ in reagents] + [0] * (8 - len(reagents))
        assert [int(server["reagentCount%d" % k]) for k in range(1, 9)] == [c for _, c in reagents] + [0] * (8 - len(reagents))
        assert int(server["effectItemType1"]) == item
        assert int(server["effectBasePoints1"]) + int(server["effectDieSides1"]) == count
        assert int(server["requiresSpellFocus"]) == {"Anvil": 1, "Forge": 3}[recipe["focus"]]
        ability = abilities[spell]
        assert {k: int(ability[k]) for k in ("skill_id", "race_mask", "class_mask", "req_skill_value",
                                             "min_value", "max_value")} == {
            "skill_id": int(recipe["skillLine"]), "race_mask": 0, "class_mask": 0,
            "req_skill_value": int(recipe["minRank"]), "min_value": int(recipe["yellow"]),
            "max_value": int(recipe["grey"])}


def test_each_item_is_in_the_item_override_table_with_its_stats_as_stock_equip_spells(out):
    items = {int(row["entry"]): row for row in sql_rows(out, "forever_item_new")}
    stock_spells, _ = read_dbc(STOCK / "Spell.dbc")
    displays, _ = read_dbc(STOCK / "ItemDisplayInfo.dbc")
    stat_types = {"Mana": 0, "Health": 1, "Agi": 3, "Str": 4, "Int": 5, "Spi": 6, "Sta": 7}
    from test_stock_items import bonus_of  # (#227) it imports this module
    for spec in new_items(out):
        server = items[int(spec["entry"])]
        stats = [] if spec["stats"] == "-" else [s.strip().split(" ") for s in spec["stats"].split(",")]
        bonuses = [] if spec["bonuses"] == "-" else [b.strip().split(" ")[:2] for b in spec["bonuses"].split(",")]
        others = [] if spec["equipSpells"] == "-" else [int(s) for s in spec["equipSpells"].split(",")]
        spells = [int(server["spellid_%d" % k]) for k in range(1, len(bonuses) + 1)] + others
        # Each bonus is a stock equip spell that gives it (#227).
        assert [bonus_of(stock_spells, s) for s in spells[:len(bonuses)]] == [(n, int(v)) for n, v in bonuses]
        assert server["name"] == spec["name"]
        assert [(int(server["stat_type%d" % k]), int(server["stat_value%d" % k])) for k in range(1, 11)] == \
            [(stat_types[n], int(v)) for n, v in stats] + [(0, 0)] * (10 - len(stats))
        assert [(int(server["spellid_%d" % k]), int(server["spelltrigger_%d" % k])) for k in range(1, 6)] == \
            [(s, ON_EQUIP) for s in spells] + [(0, 0)] * (5 - len(spells))
        assert all(s in stock_spells for s in spells)
        assert int(server["display_id"]) == int(spec["displayId"]) and int(spec["displayId"]) in displays
        assert (int(server["armor"]), int(server["item_level"]), int(server["required_level"])) == \
            (int(spec["armor"]), int(spec["itemLevel"]), int(spec["requiredLevel"]))
        assert int(server["patch"]) == 0


def test_the_tracer_item_has_its_forever_stats(out):
    # Strange Copper Boots of the Forever client (250621): rare, item level 15, level 10, bind on pickup,
    # 109 armor, Stamina 4 and spell power 5 = Increase Spell Dam 5 (9395).
    server = {int(row["entry"]): row for row in sql_rows(out, "forever_item_new")}[70000]
    assert (server["quality"], server["bonding"], server["armor"]) == ("3", "1", "109")
    assert (server["stat_type1"], server["stat_value1"], server["spellid_1"], server["spelltrigger_1"]) == \
        (str(STAMINA), "4", "9395", str(ON_EQUIP))


def test_the_item_table_is_made_again_in_the_shape_of_the_stock_table(out):
    sql = (out / "sql" / "forever_world.sql").read_text(encoding="utf-8")
    assert "DROP TABLE IF EXISTS `forever_item_template`;\nCREATE TABLE `forever_item_template` LIKE `item_template`;" in sql
    assert sql.count("INSERT INTO `forever_item_template` SELECT * FROM `forever_item_new`;") == len(new_items(out))


def test_new_recipe_and_item_ids_are_in_their_free_ranges(out):
    spell_low, spell_high = free_range("blacksmithing.tsv", "profession spell ID")
    item_low, item_high = free_range("items.tsv", "item ID")
    assert (spell_low, spell_high) == (61000, 61999) and (item_low, item_high) == (70000, 79999)
    stock, _ = read_dbc(STOCK / "Spell.dbc")
    for recipe in new_recipes():
        assert spell_low <= int(recipe["spell"]) <= spell_high and int(recipe["spell"]) not in stock
    for item in new_items():
        assert item_low <= int(item["entry"]) <= item_high
    # A recipe makes an item row of the spec or a stock item (#230: Copper Rod makes the stock 6217).
    made = {item for recipe in new_recipes() for item, _ in counted(recipe["creates"])}
    assert made - {int(item["entry"]) for item in spec_rows("items.tsv")} == {6217}


def refused(tmp_path, file, *changes):
    """The error of the generator on the spec folder with the (old, new) text `changes` made in one file. It
    must fail and write nothing."""
    import subprocess
    import sys
    from test_pipeline import GENERATOR
    folder = tmp_path / "spec"
    folder.mkdir()
    for path in SPEC_DIR.glob("*.tsv"):
        text = path.read_text(encoding="utf-8")
        if path.name == file:
            for old, new in changes:
                assert old in text, old
                text = text.replace(old, new)
        (folder / path.name).write_text(text, encoding="utf-8")
    out = tmp_path / "out"
    done = subprocess.run([sys.executable, "-I", str(GENERATOR), "--spec", str(folder), "--stock", str(STOCK),
                           "--out", str(out)], capture_output=True, text=True)
    assert done.returncode != 0 and "Traceback" not in done.stderr, done.stderr
    assert not out.exists() or not any(out.rglob("*")), "the generator wrote output"
    return done.stderr


def test_a_profession_spell_outside_its_block_is_refused(tmp_path):
    # The lead's decision on #226: profession spells take IDs from 61000-61999 only.
    error = refused(tmp_path, "blacksmithing.tsv", ("recipe\t61000\t", "recipe\t60500\t"))
    assert "outside the free profession spell ID range 61000-61999" in error


def test_a_recipe_spell_that_clashes_with_a_warrior_spell_is_refused(tmp_path):
    # Spearing Strike of warrior.tsv is spell 60100.
    error = refused(tmp_path, "blacksmithing.tsv", ("profession spell ID range: 61000-61999.", "profession spell ID range: 60100-61999."),
                    ("recipe\t61000\t", "recipe\t60100\t"))
    # #228: a Warrior spell inside the profession block is refused before the clash.
    assert "spell 60100 is inside the profession spell ID range" in error


def test_a_new_item_outside_the_free_item_range_is_refused(tmp_path):
    error = refused(tmp_path, "items.tsv", ("item\t70000\t3469\t", "item\t30000\t3469\t"))
    assert "outside the free item ID range 70000-79999" in error


def test_an_unknown_spell_focus_is_refused(tmp_path):
    # (#230: the Master Forge is a known Forever-only station, which the dead-content rule cuts.)
    error = refused(tmp_path, "blacksmithing.tsv", ("\tAnvil\t", "\tMoonwell Forge\t"))
    assert "no stock SpellFocusObject.dbc name" in error
