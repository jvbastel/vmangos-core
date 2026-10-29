"""The spec file: one row per talent (later also per new ability and changed base ability)."""
import re
from collections import namedtuple

Talent = namedtuple("Talent", "talent_id tree tier column ranks required_talent required_rank name icon port "
                              "spells source_spells effect1 tooltip")
Spec = namedtuple("Spec", "id_range talents")

TREES = ("arms", "fury", "protection")
PORTS = ("data", "C++", "substitute")


class SpecError(Exception):
    """The spec breaks a rule. The generator writes nothing."""


def _ints(text, sep):
    return [int(v) for v in text.split(sep)]


def read(path):
    with open(path, encoding="utf-8") as file:
        text = file.read()
    m = re.search(r"^# Free ID range: (\d+)-(\d+)\.", text, re.M)
    if not m:
        raise SpecError("%s: no '# Free ID range: <low>-<high>.' line in the header" % path)
    id_range = (int(m.group(1)), int(m.group(2)))

    lines = [line for line in text.splitlines() if line and not line.startswith("#")]
    header = lines[0].split("\t")
    talents = []
    for number, line in enumerate(lines[1:], start=2):
        values = line.split("\t")
        if len(values) != len(header):
            raise SpecError("%s: data line %d has %d columns, not %d" % (path, number, len(values), len(header)))
        row = dict(zip(header, values))
        if row["kind"] != "talent":
            raise SpecError("%s: data line %d: unknown kind '%s'" % (path, number, row["kind"]))
        talent = Talent(
            talent_id=int(row["talentId"]), tree=row["tree"], tier=int(row["tier"]), column=int(row["column"]),
            ranks=int(row["ranks"]), required_talent=int(row["requiredTalent"]),
            required_rank=int(row["requiredRank"]), name=row["name"], icon=int(row["icon"]), port=row["port"],
            spells=_ints(row["spells"], ","), source_spells=_ints(row["sourceSpells"], ","),
            effect1=_ints(row["effect1"], "/"), tooltip=row["tooltip"])
        if talent.tree not in TREES:
            raise SpecError("talent %d: unknown tree '%s'" % (talent.talent_id, talent.tree))
        if talent.port not in PORTS:
            raise SpecError("talent %d: unknown port type '%s'" % (talent.talent_id, talent.port))
        if not len(talent.spells) == len(talent.source_spells) == len(talent.effect1) == talent.ranks:
            raise SpecError("talent %d: spells, sourceSpells and effect1 need one value per rank (%d)"
                            % (talent.talent_id, talent.ranks))
        talents.append(talent)
    return Spec(id_range, talents)
