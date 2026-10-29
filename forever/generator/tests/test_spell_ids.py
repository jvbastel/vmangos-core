"""The spell IDs that the server code knows by number (src/game/Forever/ForeverSpellIds.h) against the spec
file forever/warrior.tsv (review T3 of jvbastel/WOWAI#204).

Each constant of the header names its spec row in a "// spec: <kind> <name>[ sources]" comment. A constant
must hold the spell IDs of that row (rank 1 first), or its source spells with "sources". So a refresh of the
spec that moves an ID fails here, not in the game.
"""
import re

import pytest

from forever_gen import build, spec
from test_pipeline import ROOT

HEADER = ROOT / "src" / "game" / "Forever" / "ForeverSpellIds.h"
FOREVER_SRC = ROOT / "src" / "game" / "Forever"
CONSTANT = re.compile(r"constexpr uint32_t (\w+)(\[\])?\s*=\s*(\{[^}]*\}|\d+)\s*;\s*//\s*spec:\s*(.+?)\s*$", re.M)


def constants():
    """[(name, [ids], spec reference)] of the header."""
    text = HEADER.read_text(encoding="utf-8")
    return [(m.group(1), [int(v) for v in re.findall(r"\d+", m.group(3))], m.group(4)) for m in CONSTANT.finditer(text)]


@pytest.fixture(scope="module")
def the_spec():
    return spec.read(ROOT / "forever" / "warrior.tsv")


def test_the_header_names_a_row_for_its_constants():
    names = [name for name, _, _ in constants()]
    assert len(names) >= 20, "the parser found too few constants: %s" % names
    for needed in ("FreeIdFirst", "VanguardTalent", "VanguardChargeRanks", "StockChargeRanks", "VictoryRushSpell",
                   "WeaponmasterRanks", "GoreDrinkerRanks", "MasterOfDefenseRanks"):
        assert needed in names


@pytest.mark.parametrize("name, ids, ref", constants(), ids=[c[0] for c in constants()])
def test_each_constant_holds_the_ids_of_its_spec_row(the_spec, name, ids, ref):
    if ref.startswith("free range "):
        low, high = the_spec.id_range
        assert ids == [low if ref == "free range first" else high], "%s: the free ID range of the spec is %d-%d" % (name, low, high)
        return
    kind, _, rest = ref.partition(" ")
    sources = rest.endswith(" sources")
    row_name = rest[: -len(" sources")] if sources else rest
    rows = [row for row in the_spec.rows if row.kind == kind and row.name == row_name]
    assert len(rows) == 1, "%s: no single %s row '%s' in warrior.tsv" % (name, kind, row_name)
    expected = rows[0].source_spells if sources else rows[0].spells
    assert ids == expected, "%s holds %s; the %s row '%s' has %s" % (name, ids, kind, row_name, expected)


def test_no_other_forever_file_writes_a_forever_spell_id(the_spec):
    """A Forever-only ID in the code comes from the header, so the check above covers it."""
    low, high = the_spec.id_range
    offenders = []
    for path in sorted(FOREVER_SRC.glob("*.[ch]*")):
        if path == HEADER:
            continue
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
            code = line.split("//")[0]
            for value in re.findall(r"\b\d{5}\b", code):
                if low <= int(value) <= high:
                    offenders.append("%s:%d: %s" % (path.name, number, value))
    assert not offenders, "use ForeverSpellIds.h for these IDs: %s" % offenders


def test_the_migration_makes_the_baseline_table_with_the_columns_of_the_generator():
    """Review S2/T7: the core reads forever_baseline_spell with the rules on. The migration makes it, so that
    it always exists, and the generator makes it again from BASELINE_COLUMNS: one shape for both."""
    migration = (ROOT / "sql" / "migrations" / "20261029120000_world.sql").read_text(encoding="utf-8")
    assert "CREATE TABLE IF NOT EXISTS `forever_baseline_spell` %s;" % build.BASELINE_COLUMNS in migration


def test_the_migration_makes_every_forever_table_that_the_core_reads():
    """Review S2: with the rules on, the core reads each forever_* table; the migration makes them, so that a
    realm without the generated SQL stops at startup (empty forever_spell_template) instead of a failed query."""
    migration = (ROOT / "sql" / "migrations" / "20261029120000_world.sql").read_text(encoding="utf-8")
    for table in build.FOREVER_TABLES:
        assert "CREATE TABLE IF NOT EXISTS `%s` LIKE `%s`;" % (table, table[len("forever_"):]) in migration, table
