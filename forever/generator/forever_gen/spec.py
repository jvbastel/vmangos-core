"""The spec: the folder forever/spec, one file per area (#226). Each file names its row kinds in its column
line. The spell file kind (warrior.tsv) has one row per talent, per new ability and per changed base ability;
the recipe rows (recipes.py) and the item rows (items.py) have their own columns.

The rules that need the stock data (tree rules, ID clashes, icons) are in rules.py. This module only reads
the rows and refuses a row that it cannot read.
"""
import os
import re
from collections import namedtuple

from . import encrypted, item_sets, items, loot, mining, recipes, trainers
from .common import SpecError, check_kind, rows_of  # noqa: F401 (SpecError: the error of the spec, for build and generate)

# kind: talent, ability (a new spell that no talent teaches) or base (a changed stock spell).
# required_talents: the prerequisite talent IDs (the tree rules allow at most one).
# effects: three lists, one per spell effect, of the value of each rank, or None to keep the source value.
# fields: {spell_template column: [value of each rank]}, see FIELDS; also nameSubtext and procEvent.<column>.
# tooltip: the description of the rank spells, or None to keep the stock description.
Row = namedtuple("Row", "kind talent_id tree tier column ranks required_talents required_rank name icon port "
                        "spells source_spells effects fields tooltip notes")
# id_range: the free spell ID range; rows: the spell rows (talent, ability, base); recipes: recipes.Recipe;
# items: items.Item; item_id_range: the free item ID range; profession_id_range: the block of the spell range
# for the profession spells (the lead's decision on #226). A range that no file names is None.
# stock_recipes: the stock rows of the recipe files (recipes.Recipe of kind stock, #228).
# removed_recipes: the removal rows of the recipe files (removed_recipes.RemovedRecipe, #229).
# drops: the ore drops of mining.tsv (mining.Drop, #232); mining_cuts: its cut rows (mining.MiningCut).
# trainers: the rows of the trainer files (trainers.Trainer, #239).
# loot_items, loot_drops, loot_rewards: the rows of the dungeon loot files (loot.LootItem, loot.Drop, loot.Reward,
# #242). The generator turns the confirmed loot items into `items` (loot.resolve).
Spec = namedtuple("Spec", "id_range rows recipes items item_id_range profession_id_range stock_recipes removed_recipes "
                          "item_sets drops mining_cuts encrypted trainers loot_items loot_drops loot_rewards",
                  defaults=((), (), None, None, (), (), (), (), (), (), (), (), (), ()))
# The free ID range lines of a spec file header: "# Free <label> range: <low>-<high>.", by Spec field.
RANGE_LABELS = {"id_range": "ID", "item_id_range": "item ID", "profession_id_range": "profession spell ID"}

KINDS = ("talent", "ability", "base")
TREES = ("arms", "fury", "protection")
PORTS = ("data", "C++", "substitute")
NO_TREE = "-"

# The spell_template columns that the `set` column may change, with their field index in Spell.dbc 1.12.1.
# The two tables have the same order (Spell.dbc has no `build` column and holds 8 locales per string).
FIELDS = {"category": 2, "attributes": 6, "attributesEx": 7, "attributesEx3": 9, "stances": 11, "casterAuraState": 16, "castingTimeIndex": 18,
          "recoveryTime": 19, "categoryRecoveryTime": 20, "interruptFlags": 21, "auraInterruptFlags": 22, "channelInterruptFlags": 23,
          "procFlags": 24, "procChance": 25, "procCharges": 26,
          "baseLevel": 28, "spellLevel": 29, "durationIndex": 30, "manaCost": 32, "rangeIndex": 36,
          # The recipe columns (recipes.py): the spell focus, the tools and the reagents.
          "requiresSpellFocus": 15, "totem1": 40, "totem2": 41,
          "equippedItemClass": 58, "equippedItemSubClassMask": 59,
          # The global cooldown: its category (133 for the Warrior abilities) and its time in ms.
          "startRecoveryCategory": 157, "startRecoveryTime": 158,
          # 64 bits: Spell.dbc holds the low word here and the high word in the next field.
          "spellFamilyFlags": 161}
for _k in range(8):
    FIELDS["reagent%d" % (_k + 1)] = 42 + _k
    FIELDS["reagentCount%d" % (_k + 1)] = 50 + _k
for _k in range(3):
    for _name, _first in (("effect", 61), ("effectDieSides", 64), ("effectBaseDice", 67), ("effectBasePoints", 76),
                          ("effectImplicitTargetA", 82), ("effectImplicitTargetB", 85),
                          ("effectRadiusIndex", 88), ("effectApplyAuraName", 91),
                          ("effectItemType", 103), ("effectMiscValue", 106), ("effectTriggerSpell", 109)):
        FIELDS["%s%d" % (_name, _k + 1)] = _first + _k
    # A float: the points that effect k gains per caster level above the spellLevel (up to maxLevel).
    FIELDS["effectRealPointsPerLevel%d" % (_k + 1)] = 73 + _k
# The `set` columns that hold a float (Spell.dbc stores the 32-bit float; the SQL a decimal).
FLOAT_FIELDS = frozenset("effectRealPointsPerLevel%d" % (k + 1) for k in range(3))
# A text field: the rank text of the spellbook ("Rank 2").
NAME_SUBTEXT = "nameSubtext"
# procEvent.<column>: a column of the spell_proc_event row of the row's first rank (the core gives the
# higher ranks the row of the first rank). One value for all ranks. The generator copies the stock row of
# that spell, or starts from an empty one, and writes it to forever_spell_proc_event.
PROC_EVENT = "procEvent."
PROC_EVENT_COLUMNS = ("SchoolMask", "SpellFamilyName", "SpellFamilyMask0", "SpellFamilyMask1", "SpellFamilyMask2",
                      "procFlags", "procEx", "Cooldown")
# A text field of the server row only: the script of the spell (spell_template.script_name). The core
# binds only script names that a stock table has, so a Forever row can keep its stock script or drop it.
SCRIPT_NAME = "scriptName"
# The learning keys (#212), not spell columns:
#   skillLine     a new spell gets a skill line ability (ID = the spell ID) in this skill line, for the
#                 Warrior: the spellbook tab, and the spell chain of its ability (SpellMgr::LoadSpellChains)
#   supersededBy  the next rank of the skill line ability; default: the next spell of the row, else none
#   baseline      1: the core teaches the spell at its spellLevel with no trainer (forever_baseline_spell)
SKILL_LINE, SUPERSEDED_BY, BASELINE = "skillLine", "supersededBy", "baseline"
# threat: the flat threat of each rank on every target hit (a forever_spell_threat row that replaces the
# stock spell_threat row of the rank; multiplier 1). A server key, not a spell column.
THREAT = "threat"
TEXT_KEYS = (NAME_SUBTEXT, SCRIPT_NAME)
GENERATOR_KEYS = TEXT_KEYS + (SKILL_LINE, SUPERSEDED_BY, BASELINE, THREAT)

COLUMNS = ("kind", "talentId", "tree", "tier", "column", "ranks", "requiredTalent", "requiredRank", "name",
           "icon", "port", "spells", "sourceSpells", "effect1", "effect2", "effect3", "set", "tooltip", "notes")


def _int(text):
    return int(text, 0)


def _ints(text, sep):
    return [_int(v) for v in text.split(sep)]


def _per_rank(where, text, ranks, parse=_int):
    values = [parse(v) for v in text.split("/")]
    if len(values) == 1:
        values = values * ranks
    if len(values) != ranks:
        raise SpecError("%s: %d values for %d ranks" % (where, len(values), ranks))
    return values


def _fields(where, text, ranks):
    if text == "-":
        return {}
    fields = {}
    for item in text.split(";"):
        name, _, value = item.partition("=")
        proc_event = name.startswith(PROC_EVENT)
        if proc_event and name[len(PROC_EVENT):] not in PROC_EVENT_COLUMNS:
            raise SpecError("%s: unknown proc event column '%s'" % (where, name))
        if not proc_event and name not in FIELDS and name not in GENERATOR_KEYS:
            raise SpecError("%s: unknown set field '%s'" % (where, name))
        if proc_event and "/" in value:
            raise SpecError("%s: %s takes one value for all ranks" % (where, name))
        parse = str if name in TEXT_KEYS else float if name in FLOAT_FIELDS else _int
        fields[name] = _per_rank("%s %s" % (where, name), value, ranks, parse)
    return fields


def _id_range(text, label):
    m = re.search(r"^# Free %s range: (\d+)-(\d+)\." % label, text, re.M)
    return (int(m.group(1)), int(m.group(2))) if m else None


def data_lines(text):
    """The lines of a spec file that are no comments: the column line first, then the rows."""
    return [line for line in text.splitlines() if line and not line.startswith("#")]


def read(path):
    """The spec of a folder (every *.tsv file in it, by name) or of one spec file. Each file is read by the
    reader of its column line. A free ID range ("# Free ID range: <low>-<high>." for spells, "# Free item
    ID range: ..." for items) is named by one file and holds for all."""
    if os.path.isdir(path):
        files = [os.path.join(path, name) for name in sorted(os.listdir(path)) if name.endswith(".tsv")]
    else:
        files = [path]
    readers = {COLUMNS: _read_spell_rows, recipes.COLUMNS: recipes.read_rows, items.COLUMNS: items.read_rows,
               item_sets.COLUMNS: item_sets.read_rows, item_sets.DUNGEON_COLUMNS: item_sets.read_dungeon_rows,
               mining.COLUMNS: mining.read_rows,
               encrypted.COLUMNS: encrypted.read_rows, trainers.COLUMNS: trainers.read_rows,
               loot.ITEM_COLUMNS: loot.read_item_rows, loot.LOOT_COLUMNS: loot.read_loot_rows}
    found = {"rows": [], "recipes": [], "items": [], "stock_recipes": [], "removed_recipes": [], "item_sets": [],
             "drops": [], "mining_cuts": [], "encrypted": [], "trainers": [], "loot_items": [], "loot_drops": [],
             "loot_rewards": []}
    ranges = {}
    for file in files:
        with open(file, encoding="utf-8") as handle:
            text = handle.read()
        for field, label in RANGE_LABELS.items():
            id_range = _id_range(text, label)
            if id_range and ranges.setdefault(field, id_range) != id_range:
                raise SpecError("%s: the free %s range %d-%d is not the one of another spec file" % ((file, label) + id_range))
        lines = data_lines(text)
        header = tuple(lines[0].split("\t")) if lines else ()
        if header not in readers:
            raise SpecError("%s: the column line is that of no spec file kind (spells: %s)" % (file, "\t".join(COLUMNS)))
        for key, rows in readers[header](file, lines).items():
            found[key] += rows
    if "id_range" not in ranges:
        raise SpecError("%s: no '# Free ID range: <low>-<high>.' line in the header" % path)
    return Spec(ranges["id_range"], found["rows"], found["recipes"], found["items"], ranges.get("item_id_range"),
                ranges.get("profession_id_range"), found["stock_recipes"], found["removed_recipes"], found["item_sets"],
                found["drops"], found["mining_cuts"], found["encrypted"], found["trainers"], found["loot_items"],
                found["loot_drops"], found["loot_rewards"])


def _read_spell_rows(path, lines):
    rows = []
    for where, raw in rows_of(path, lines, COLUMNS):
        check_kind(where, raw["kind"], KINDS)
        ranks = int(raw["ranks"])
        effects = [None if raw[c] == "-" else _per_rank("%s %s" % (where, c), raw[c], ranks)
                   for c in ("effect1", "effect2", "effect3")]
        row = Row(
            kind=raw["kind"], talent_id=int(raw["talentId"]), tree=raw["tree"], tier=int(raw["tier"]),
            column=int(raw["column"]), ranks=ranks,
            required_talents=[t for t in _ints(raw["requiredTalent"], ",") if t],
            required_rank=int(raw["requiredRank"]), name=raw["name"], icon=int(raw["icon"]), port=raw["port"],
            spells=_ints(raw["spells"], ","), source_spells=_ints(raw["sourceSpells"], ","), effects=effects,
            fields=_fields(where, raw["set"], ranks), tooltip=None if raw["tooltip"] == "-" else raw["tooltip"],
            notes=raw["notes"])
        if row.port not in PORTS:
            raise SpecError("%s: unknown port type '%s'" % (where, row.port))
        if row.kind == "talent" and row.tree not in TREES:
            raise SpecError("%s: unknown tree '%s'" % (where, row.tree))
        if row.kind != "talent" and (row.tree != NO_TREE or row.talent_id or row.tier or row.column
                                     or row.required_talents):
            raise SpecError("%s: an %s row has no talent ID, tree, tier, column or prerequisite" % (where, row.kind))
        if not len(row.spells) == len(row.source_spells) == ranks:
            raise SpecError("%s: spells and sourceSpells need one value per rank (%d)" % (where, ranks))
        rows.append(row)
    return {"rows": rows}


def talents(spec):
    return [row for row in spec.rows if row.kind == "talent"]


def proc_event(row):
    """{spell_proc_event column: value} that the row sets on the proc event of its first rank."""
    return {name[len(PROC_EVENT):]: values[0] for name, values in row.fields.items() if name.startswith(PROC_EVENT)}
