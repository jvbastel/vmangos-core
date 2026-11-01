#!/usr/bin/env python3
"""Writes the stock world data that the loot rows of the spec need (spec jvbastel/WOWAI#240, #242) into
forever_gen/stock_world/. The generator has no world database, so it reads these files instead.

    uv run python tools/stock_world_rows.py [--realm <realm.sh options>]

It reads the stock world database of a test realm with `wowai/test-realm/realm.sh <options> sql mangos <query>`
(SELECT only). The default options are `--shared`: the shared test realm, whose world database is stock (the
Forever tables are separate tables). Never edit the written files by hand. Run it again when the stock world
database changes (an upstream merge) or a loot ticket needs a column that the files do not have.

Files (tab-separated, with a header line, sorted by the first column):
- creatures.tsv: entry, loot_id, name of every creature_template entry with creature loot (the newest patch row).
  The boss of a drop row must be one, with this name.
- quests.tsv: entry, title, choices (the six RewChoiceItemId, comma-separated, 0 = empty) of every quest
  (the newest patch row).
- references.tsv: entry, users (the creature loot entries that name the reference) of every reference_loot_template
  entry.
- gear.tsv: entry, class, subclass, inventory_type, item_level, quality, display_id, disenchant_id of the stock
  weapons and armor
  (class 2 and 4) of quality uncommon to epic that a new item may copy: a patch 0 row with no flags, no class,
  race, skill, spell, honor or reputation need, no quest, no time limit and no area or map bond.
"""
import argparse
import os
import subprocess
import sys

GENERATOR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.dirname(os.path.dirname(GENERATOR))
REALM = os.path.join(ROOT, "wowai", "test-realm", "realm.sh")
OUT = os.path.join(GENERATOR, "forever_gen", "stock_world")

NEWEST = "t1.`patch`=(SELECT MAX(t2.`patch`) FROM `%s` t2 WHERE t2.`entry`=t1.`entry`)"
QUERIES = {
    "creatures.tsv": (("entry", "loot_id", "name"),
                      "SELECT t1.`entry`, t1.`loot_id`, t1.`name` FROM `creature_template` t1 WHERE t1.`loot_id`<>0 AND "
                      + NEWEST % "creature_template" + " ORDER BY t1.`entry`"),
    "quests.tsv": (("entry", "title", "choices"),
                   "SELECT t1.`entry`, REPLACE(REPLACE(IFNULL(t1.`Title`, ''), '\\t', ' '), '\\n', ' '), CONCAT_WS(',', "
                   + ", ".join("t1.`RewChoiceItemId%d`" % k for k in range(1, 7)) + ") FROM `quest_template` t1 WHERE "
                   + NEWEST % "quest_template" + " ORDER BY t1.`entry`"),
    "references.tsv": (("entry", "users"),
                       "SELECT r.`entry`, IFNULL((SELECT GROUP_CONCAT(DISTINCT c.`entry` ORDER BY c.`entry`) FROM "
                       "`creature_loot_template` c WHERE c.`mincountOrRef`=-r.`entry`), '') FROM (SELECT DISTINCT `entry` "
                       "FROM `reference_loot_template`) r ORDER BY r.`entry`"),
    "gear.tsv": (("entry", "class", "subclass", "inventory_type", "item_level", "quality", "display_id", "disenchant_id"),
                 "SELECT `entry`, `class`, `subclass`, `inventory_type`, `item_level`, `quality`, `display_id`, `disenchant_id` FROM "
                 "`item_template` WHERE `patch`=0 AND `class` IN (2, 4) AND `quality` BETWEEN 2 AND 4 AND `flags`=0 AND "
                 "`allowable_class`=-1 AND `allowable_race`=-1 AND `required_skill`=0 AND `required_spell`=0 AND "
                 "`required_honor_rank`=0 AND `required_reputation_faction`=0 AND `start_quest`=0 AND `duration`=0 AND "
                 "`area_bound`=0 AND `map_bound`=0 ORDER BY `entry`"),
}
HEADER = "# Stock 1.12 world data (build 5875), written by forever/generator/tools/stock_world_rows.py. Never edit by hand."


def query(realm_args, sql):
    done = subprocess.run([REALM] + realm_args + ["sql", "mangos", sql], check=True, capture_output=True, text=True)
    return [line.split("\t") for line in done.stdout.splitlines() if line]


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--realm", default="--shared", help="the realm.sh options before 'sql' (default: --shared)")
    args = parser.parse_args(argv)
    os.makedirs(OUT, exist_ok=True)
    for name, (columns, sql) in QUERIES.items():
        rows = query(args.realm.split(), sql)
        with open(os.path.join(OUT, name), "w", encoding="utf-8", newline="\n") as handle:
            handle.write(HEADER + "\n" + "\t".join(columns) + "\n")
            handle.writelines("\t".join(row) + "\n" for row in rows)
        print("%s: %d rows" % (name, len(rows)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
