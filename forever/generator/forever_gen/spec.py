"""The spec file: one row per talent, per new ability and per changed base ability.

The rules that need the stock data (tree rules, ID clashes, icons) are in rules.py. This module only reads
the rows and refuses a row that it cannot read.
"""
import re
from collections import namedtuple

# kind: talent, ability (a new spell that no talent teaches) or base (a changed stock spell).
# required_talents: the prerequisite talent IDs (the tree rules allow at most one).
# effects: three lists, one per spell effect, of the value of each rank, or None to keep the source value.
# fields: {spell_template column: [value of each rank]}, see FIELDS.
# tooltip: the description of the rank spells, or None to keep the stock description.
Row = namedtuple("Row", "kind talent_id tree tier column ranks required_talents required_rank name icon port "
                        "spells source_spells effects fields tooltip notes")
Spec = namedtuple("Spec", "id_range rows")

KINDS = ("talent", "ability", "base")
TREES = ("arms", "fury", "protection")
PORTS = ("data", "C++", "substitute")
NO_TREE = "-"

# The spell_template columns that the `set` column may change, with their field index in Spell.dbc 1.12.1.
# The two tables have the same order (Spell.dbc has no `build` column and holds 8 locales per string).
FIELDS = {"category": 2, "attributes": 6, "attributesEx3": 9, "stances": 11, "casterAuraState": 16, "castingTimeIndex": 18,
          "recoveryTime": 19, "categoryRecoveryTime": 20, "interruptFlags": 21, "procFlags": 24, "procChance": 25, "procCharges": 26,
          "baseLevel": 28, "spellLevel": 29, "durationIndex": 30, "manaCost": 32, "rangeIndex": 36,
          "equippedItemClass": 58, "equippedItemSubClassMask": 59}
for _k in range(3):
    for _name, _first in (("effect", 61), ("effectDieSides", 64), ("effectBaseDice", 67), ("effectBasePoints", 76),
                          ("effectImplicitTargetA", 82), ("effectImplicitTargetB", 85),
                          ("effectRadiusIndex", 88), ("effectApplyAuraName", 91),
                          ("effectItemType", 103), ("effectMiscValue", 106), ("effectTriggerSpell", 109)):
        FIELDS["%s%d" % (_name, _k + 1)] = _first + _k
# A text field: the rank text of the spellbook ("Rank 2").
NAME_SUBTEXT = "nameSubtext"
# A text field of the server row only: the script of the spell (spell_template.script_name). The core
# binds only script names that a stock table has, so a Forever row can keep its stock script or drop it.
SCRIPT_NAME = "scriptName"
# The learning keys (#212), not spell columns:
#   skillLine     a new spell gets a skill line ability (ID = the spell ID) in this skill line, for the
#                 Warrior: the spellbook tab, and the spell chain of its ability (SpellMgr::LoadSpellChains)
#   supersededBy  the next rank of the skill line ability; default: the next spell of the row, else none
#   baseline      1: the core teaches the spell at its spellLevel with no trainer (forever_baseline_spell)
SKILL_LINE, SUPERSEDED_BY, BASELINE = "skillLine", "supersededBy", "baseline"
TEXT_KEYS = (NAME_SUBTEXT, SCRIPT_NAME)
GENERATOR_KEYS = TEXT_KEYS + (SKILL_LINE, SUPERSEDED_BY, BASELINE)

COLUMNS = ("kind", "talentId", "tree", "tier", "column", "ranks", "requiredTalent", "requiredRank", "name",
           "icon", "port", "spells", "sourceSpells", "effect1", "effect2", "effect3", "set", "tooltip", "notes")


class SpecError(Exception):
    """The spec breaks a rule. The generator writes nothing."""


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
        if name not in FIELDS and name not in GENERATOR_KEYS:
            raise SpecError("%s: unknown set field '%s'" % (where, name))
        fields[name] = _per_rank("%s %s" % (where, name), value, ranks, str if name in TEXT_KEYS else _int)
    return fields


def read(path):
    with open(path, encoding="utf-8") as file:
        text = file.read()
    m = re.search(r"^# Free ID range: (\d+)-(\d+)\.", text, re.M)
    if not m:
        raise SpecError("%s: no '# Free ID range: <low>-<high>.' line in the header" % path)
    id_range = (int(m.group(1)), int(m.group(2)))

    lines = [line for line in text.splitlines() if line and not line.startswith("#")]
    header = tuple(lines[0].split("\t"))
    if header != COLUMNS:
        raise SpecError("%s: the column line is not %s" % (path, "\t".join(COLUMNS)))
    rows = []
    for number, line in enumerate(lines[1:], start=2):
        values = line.split("\t")
        if len(values) != len(header):
            raise SpecError("%s: data line %d has %d columns, not %d" % (path, number, len(values), len(header)))
        raw = dict(zip(header, values))
        where = "%s (data line %d)" % (raw["name"], number)
        if raw["kind"] not in KINDS:
            raise SpecError("%s: unknown kind '%s'" % (where, raw["kind"]))
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
    return Spec(id_range, rows)


def talents(spec):
    return [row for row in spec.rows if row.kind == "talent"]
