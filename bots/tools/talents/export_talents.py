"""Writes the talent table of Talent.dbc and TalentTab.dbc as TSV.

Used by export-talents.sh. Arguments: <dbc folder> <spell names TSV>. The
spell names file has `<spellId> TAB <name>` lines.
"""
import struct
import sys

# Bits of ChrClasses in TalentTab.dbc ClassMask, with the class words of the
# bot data files. The Paladin is Alliance only and has no word.
CLASS_BITS = {
    1: "warrior", 2: "paladin", 4: "hunter", 8: "rogue", 16: "priest",
    64: "shaman", 128: "mage", 256: "warlock", 1024: "druid",
}


def read_dbc(path):
    with open(path, "rb") as file:
        data = file.read()
    magic, records, fields, size, _strings = struct.unpack_from("<4s4I", data, 0)
    if magic != b"WDBC" or size != fields * 4:
        raise SystemExit(path + ": not a WDBC file of 4-byte fields")
    strings = data[20 + records * size:]
    rows = [struct.unpack_from("<%dI" % fields, data, 20 + i * size) for i in range(records)]
    return rows, lambda offset: strings[offset:strings.index(b"\0", offset)].decode("utf-8")


def main():
    dbc, names_path = sys.argv[1], sys.argv[2]
    names = {}
    with open(names_path, encoding="utf-8") as file:
        for line in file:
            parts = line.rstrip("\n").split("\t")
            if len(parts) >= 2 and parts[0].isdigit():
                names[int(parts[0])] = parts[1]

    # TalentTab.dbc: 0 ID, 1 Name (enUS), 12 ClassMask. The tree word is the
    # name in lower case with `-` for spaces, for example `beast-mastery`.
    # (OrderIndex is no help: Arcane and Fire both have 0.)
    tabs = {}
    rows, text = read_dbc(dbc + "/TalentTab.dbc")
    for row in rows:
        if row[12] in CLASS_BITS:
            tabs[row[0]] = (CLASS_BITS[row[12]], text(row[1]).lower().replace(" ", "-"))

    # Talent.dbc: 0 ID, 1 TabID, 2 Row, 3 Column, 4-12 RankID, 13-15 DependsOn,
    # 16-18 DependsOnRank (0 = rank 1), 19 Flags, 20 DependsOnSpell.
    print("# Talents of Talent.dbc (1.12.1). Made by bots/tools/talents/export-talents.sh.")
    print("# talentId\tclass\ttree\ttier\tcolumn\tmaxRank\trequiredTalent\trequiredRank\tname")
    rows = []
    for row in read_dbc(dbc + "/Talent.dbc")[0]:
        if row[1] not in tabs:
            continue
        botClass, tree = tabs[row[1]]
        ranks = [spell for spell in row[4:9] if spell]
        required = row[13]
        requiredRank = row[16] + 1 if required else 0
        # DependsOnSpell (row[20]) is left out: only Nature's Grasp has one,
        # a trainer spell that the level routine teaches first.
        rows.append((row[0], botClass, tree, row[2] + 1, row[3] + 1, len(ranks), required, requiredRank,
                     names.get(ranks[0], "?") if ranks else "?"))
    for r in sorted(rows):
        print("\t".join(str(v) for v in r))


main()
