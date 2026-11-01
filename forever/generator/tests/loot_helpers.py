"""Helpers of the dungeon loot tests (spec #240): they read the generated SQL and the loot list from the outside.

One copy for every dungeon test (#240 review M6). The dungeon tests keep only their expected rows.
"""
import re

from test_blacksmithing import spec_rows, sql_rows
from test_new_recipes import run, spec_folder
from test_pipeline import ROOT, SPEC_DIR, STAT_TYPES, bonus_of  # noqa: F401 (bonus_of: for the dungeon tests)

QUEST = "forever_quest_template"
CREATURE_LOOT, REFERENCE_LOOT = "forever_creature_loot_template", "forever_reference_loot_template"
LOOT_ROW = (r"^INSERT INTO `%s` \(`entry`, `item`, `ChanceOrQuestChance`, `groupid`, `mincountOrRef`, `maxcount`, "
            r"`patch_min`, `patch_max`\) VALUES \(([^)]*)\);$")
# The item_template spelltrigger of an on-equip spell, as the generated SQL writes it.
ON_EQUIP = "1"
# The stock world data of the generator: the stock gear that a new item may copy.
GEAR = ROOT / "forever" / "generator" / "forever_gen" / "stock_world" / "gear.tsv"
# The dungeon spec files: (items file, loot file) per dungeon slug.
SLUGS = ("ragefire_chasm", "wailing_caverns", "deadmines", "shadowfang_keep", "blackfathom_deeps", "stockade")
ITEM_FILES = ["loot_%s_items.tsv" % slug for slug in SLUGS]
LOOT_FILES = ["loot_%s.tsv" % slug for slug in SLUGS]


def stock_gear():
    """{entry: (class, subclass, slot, display)} of the stock gear file."""
    rows = [line.split("\t") for line in GEAR.read_text(encoding="utf-8").splitlines()[2:]]
    return {int(r[0]): (int(r[1]), int(r[2]), int(r[3]), int(r[6])) for r in rows}


def sql(out):
    return (out / "sql" / "forever_world.sql").read_text(encoding="utf-8")


def item_rows(out, temp):
    """{entry: {column: value}} of the item rows on the temporary copy table `temp` (forever_item_stock: changed
    stock items, forever_item_new: new items)."""
    return {int(r["entry"]): r for r in sql_rows(out, temp) if "entry" in r}


def loot_rows(text, table=CREATURE_LOOT):
    """{(entry, item): (chance, group, min count, max count, patch_min, patch_max)} of the new loot rows."""
    rows = {}
    for values in re.findall(LOOT_ROW % table, text, re.M):
        entry, item, chance, *rest = values.split(", ")
        rows[(int(entry), int(item))] = (float(chance),) + tuple(int(v) for v in rest)
    return rows


def copied_entries(text, table=CREATURE_LOOT):
    """The loot entries whose stock rows the SQL copies into `table`."""
    return {int(e) for e in re.findall(r"^INSERT INTO `%s` SELECT \* FROM `%s` WHERE `entry`=(\d+);$"
                                       % (table, table[len("forever_"):]), text, re.M)}


def stats_of(row):
    """{stat name: value} of the stat slots of a generated item row."""
    names = {str(v): k for k, v in STAT_TYPES.items()}
    return {names[row["stat_type%d" % k]]: int(row["stat_value%d" % k]) for k in range(1, 11)
            if row["stat_type%d" % k] != "0"}


def weapon_of(row):
    """((min, max) damage, delay) of a generated item row."""
    return (int(row["dmg_min1"]), int(row["dmg_max1"])), int(row["delay"])


def stock_item_block(text, name, entry):
    """The SQL of the changed stock item `entry`, from its comment to its insert."""
    block = text[text.index("-- Changed stock item %s (%d)." % (name, entry)):]
    return block[:block.index("INSERT INTO `forever_item_template`")]


def stock_equip_spells(text, name, entry):
    """The on-equip spells that the SQL puts on the changed stock item `entry` (items.py: each into the first empty
    slot of the copy of the stock row)."""
    return [int(s) for s in re.findall(r"`spellid_1`=IF\(@forever_slot=1, (\d+),", stock_item_block(text, name, entry))]


def emptied_equip_slots(text, name, entry):
    """True when the SQL empties the stock on-equip slots of the changed stock item `entry`."""
    return "WHERE `spelltrigger_1`=%s;" % ON_EQUIP in stock_item_block(text, name, entry)


def new_equip_spells(row):
    """The on-equip spells of a generated new item row."""
    return [int(row["spellid_%d" % k]) for k in range(1, 6) if row["spelltrigger_%d" % k] == ON_EQUIP]


def quest_changes(out):
    """{quest: {column: value}} of the reward changes on the quest copies."""
    changes = {}
    for m in re.finditer(r"^-- Quest (\d+) .*\n(?:.*\n)*?UPDATE `forever_quest_new` SET (.*);$", sql(out), re.M):
        changes[int(m.group(1))] = dict(re.findall(r"`(\w+)`=(\d+)", m.group(2)))
    return changes


def loot_list(out):
    """{(list, kind, id): detail} of the loot list."""
    lines = (out / "loot-list.tsv").read_text(encoding="utf-8").splitlines()[2:]
    return {tuple(line.split("\t")[:3]): line.split("\t")[4] for line in lines}


def ours(rows, entries):
    """The rows of `rows` (a dict keyed by loot entry or by (loot entry, item)) whose loot entry is in `entries`."""
    return {k: v for k, v in rows.items() if (k[0] if isinstance(k, tuple) else k) in entries}


def item_block(name):
    """(low, high) of the `# Item ID block:` header line of an items file."""
    m = re.search(r"^# Item ID block: (\d+)-(\d+)\.", (SPEC_DIR / name).read_text(encoding="utf-8"), re.M)
    return int(m.group(1)), int(m.group(2))


def spec_drops(name):
    """{(boss, item): chance} of the confirmed drop rows of a loot file."""
    return {(int(r["target"]), int(r["item"])): float(r["chance"]) for r in spec_rows(name)
            if r["kind"] == "drop" and r["status"] == "confirmed"}


def changed(tmp_path, *changes, extra=()):
    """The output folder of the generator on the spec folder with the (file, old, new) text changes and the extra
    (file, line) lines, and its run."""
    folder = spec_folder(tmp_path, changes=changes)
    for name, line in extra:
        with open(folder / name, "a", encoding="utf-8") as handle:
            handle.write(line + "\n")
    out = tmp_path / "out"
    return out, run(folder, out)


def refused(tmp_path, *changes, extra=()):
    """The error of a generator run that must refuse the changed spec folder and write nothing."""
    out, done = changed(tmp_path, *changes, extra=extra)
    assert done.returncode != 0 and "Traceback" not in done.stderr, done.stderr
    assert not out.exists() or not any(out.rglob("*")), "the generator wrote output"
    return done.stderr
