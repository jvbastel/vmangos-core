"""The four places that name each Forever override table agree (#242 follow-up: a new table needs an edit in each).

The generator makes the tables (build.FOREVER_TABLES), the core stops at startup when one is missing
(Forever.cpp foreverTables[]), a world migration makes each one, and LootMgr.cpp reads each Forever loot table. A
new table that misses one of these places fails here, before a build.
"""
import re

from forever_gen import build
from test_pipeline import ROOT

TABLES = set(build.FOREVER_TABLES) | {build.BASELINE_TABLE}


def test_the_core_stops_at_startup_without_each_forever_table():
    text = (ROOT / "src" / "game" / "Forever" / "Forever.cpp").read_text(encoding="utf-8")
    block = re.search(r"foreverTables\[\] = \{(.*?)\};", text, re.S).group(1)
    assert set(re.findall(r'"(forever_\w+)"', block)) == TABLES


def test_a_world_migration_makes_each_forever_table():
    made = set()
    for path in (ROOT / "sql" / "migrations").glob("*_world.sql"):
        made |= set(re.findall(r"CREATE TABLE IF NOT EXISTS `(forever_\w+)`", path.read_text(encoding="utf-8")))
    assert made == TABLES


def test_the_core_reads_each_forever_loot_table():
    stores = {"forever_gameobject_loot_template": "Gameobject", "forever_creature_loot_template": "Creature",
              "forever_reference_loot_template": "Reference"}
    assert {t for t in TABLES if t.endswith("_loot_template")} == set(stores)
    text = (ROOT / "src" / "game" / "LootMgr.cpp").read_text(encoding="utf-8")
    line = next(line for line in text.splitlines() if "Forever::Table(GetName()" in line)
    assert set(re.findall(r"&LootTemplates_(\w+)", line)) == set(stores.values())
