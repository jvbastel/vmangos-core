"""The data pipeline test of the Forever rules (spec jvbastel/WOWAI#204, testing decision 1).

It runs the generator on the spec file, as the test realm kit does, and reads what it wrote from the
outside: the talent tables through the talent exporter of the bot tools, the client spell rows as a
DBC file, and the server spell data as the SQL that the world database runs.

It needs the stock 1.12.1 DBC files of the extracted client data (FOREVER_STOCK_DBC, default the
folder of vmangos-deploy). Without them it skips.
"""
import os
import re
import struct
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[3]
SPEC = ROOT / "forever" / "warrior.tsv"
GENERATOR = ROOT / "forever" / "generator" / "generate.py"
EXPORTER = ROOT / "bots" / "tools" / "talents" / "export_talents.py"
STOCK = Path(os.environ.get("FOREVER_STOCK_DBC", "/mnt/c/Games/vmangos-deploy/storage/mangosd/extracted-data/5875/dbc"))

pytestmark = pytest.mark.skipif(not (STOCK / "Spell.dbc").is_file(), reason="no stock DBC folder at %s" % STOCK)

# Spell.dbc 1.12.1: 0 ID, 117 SpellIconID, 120 Name (enUS), 129 Rank (enUS), 138 Description (enUS).
SPELL_ID, SPELL_ICON, SPELL_NAME, SPELL_RANK, SPELL_DESCRIPTION = 0, 117, 120, 129, 138


def spec_rows():
    lines = [line for line in SPEC.read_text(encoding="utf-8").splitlines() if line and not line.startswith("#")]
    header = lines[0].split("\t")
    return [dict(zip(header, line.split("\t"))) for line in lines[1:]]


def free_range():
    m = re.search(r"^# Free ID range: (\d+)-(\d+)\.", SPEC.read_text(encoding="utf-8"), re.M)
    return int(m.group(1)), int(m.group(2))


def read_dbc(path):
    data = Path(path).read_bytes()
    magic, records, fields, size, _ = struct.unpack_from("<4s4I", data, 0)
    assert magic == b"WDBC" and size == fields * 4
    strings = data[20 + records * size:]
    rows = {}
    for i in range(records):
        row = struct.unpack_from("<%dI" % fields, data, 20 + i * size)
        rows[row[0]] = row
    return rows, lambda offset: strings[offset:strings.index(b"\0", offset)].decode("utf-8")


def generate(out):
    subprocess.run([sys.executable, "-I", str(GENERATOR), "--spec", str(SPEC), "--stock", str(STOCK),
                    "--out", str(out)], check=True)
    return out


def export(dbc_folder, tmp_path):
    names = tmp_path / "names.tsv"
    names.write_text("".join("%s\t%s\n" % (row["spells"].split(",")[0], row["name"]) for row in spec_rows()))
    done = subprocess.run([sys.executable, "-I", str(EXPORTER), str(dbc_folder), str(names)],
                          check=True, capture_output=True, text=True)
    return [line.split("\t") for line in done.stdout.splitlines() if not line.startswith("#")]


@pytest.fixture(scope="module")
def out(tmp_path_factory):
    return generate(tmp_path_factory.mktemp("forever"))


def server_spells(out):
    """entry -> the columns that the generated SQL sets on the Forever copy of a spell row."""
    sql = (out / "sql" / "forever_world.sql").read_text(encoding="utf-8")
    spells = {}
    for m in re.finditer(r"^UPDATE `forever_spell_new` SET (.*);$", sql, re.M):
        values = dict(re.findall(r"`(\w+)`=('(?:[^'\\]|\\.)*'|-?\d+)", m.group(1)))
        spells[int(values["entry"])] = {k: v.strip("'") for k, v in values.items()}
    return spells


def test_the_exported_warrior_trees_are_the_spec_tree(out, tmp_path):
    warrior = sorted(row for row in export(out / "dbc", tmp_path) if row[1] == "warrior")
    expected = sorted([r["talentId"], "warrior", r["tree"], r["tier"], r["column"], r["ranks"],
                       r["requiredTalent"], r["requiredRank"], r["name"]] for r in spec_rows())
    assert warrior == expected


def test_the_other_classes_keep_their_stock_trees(out, tmp_path):
    forever = [row[:8] for row in export(out / "dbc", tmp_path) if row[1] != "warrior"]
    stock = [row[:8] for row in export(STOCK, tmp_path) if row[1] != "warrior"]
    assert len(stock) > 300
    assert forever == stock


def test_the_client_gets_the_same_talent_tables_as_the_server(out):
    for name in ("Talent.dbc", "TalentTab.dbc"):
        assert (out / "client" / "DBFilesClient" / name).read_bytes() == (out / "dbc" / name).read_bytes()


def test_each_spec_spell_is_in_the_client_and_the_server_with_its_rank_and_icon(out):
    client, text = read_dbc(out / "client" / "DBFilesClient" / "Spell.dbc")
    server = server_spells(out)
    for row in spec_rows():
        for rank, spell in enumerate(int(s) for s in row["spells"].split(",")):
            assert spell in client, spell
            assert text(client[spell][SPELL_NAME]) == row["name"]
            assert text(client[spell][SPELL_RANK]) == "Rank %d" % (rank + 1)
            assert client[spell][SPELL_ICON] == int(row["icon"])
            assert spell in server, spell
            assert server[spell]["name"] == row["name"]
            assert server[spell]["nameSubtext"] == "Rank %d" % (rank + 1)
            assert int(server[spell]["spellIconId"]) == int(row["icon"])


def test_a_rank_has_the_spec_value_and_tooltip(out):
    # Improved Rend rank 2 of build 70450: 23%. A DBC holds a value as base points + 1.
    client, text = read_dbc(out / "client" / "DBFilesClient" / "Spell.dbc")
    assert client[60001][76] + 1 == 23
    assert text(client[60001][SPELL_DESCRIPTION]) == "Increases the Bleed damage done by your Rend ability by $s1%."
    assert int(server_spells(out)[60001]["effectBasePoints1"]) + 1 == 23


def test_the_stock_spell_rows_stay_in_the_client_table(out):
    client, text = read_dbc(out / "client" / "DBFilesClient" / "Spell.dbc")
    stock, stock_text = read_dbc(STOCK / "Spell.dbc")
    assert set(stock) <= set(client)
    assert text(client[12286][SPELL_NAME]) == stock_text(stock[12286][SPELL_NAME]) == "Improved Rend"
    assert client[12286][:SPELL_NAME] == stock[12286][:SPELL_NAME]


def test_new_ids_are_in_the_free_range(out):
    low, high = free_range()
    stock_spells, _ = read_dbc(STOCK / "Spell.dbc")
    stock_talents, _ = read_dbc(STOCK / "Talent.dbc")
    for row in spec_rows():
        if int(row["talentId"]) not in stock_talents:
            assert low <= int(row["talentId"]) <= high
        for spell in (int(s) for s in row["spells"].split(",")):
            assert spell not in stock_spells
            assert low <= spell <= high


def test_two_runs_give_the_same_bytes(out, tmp_path):
    again = generate(tmp_path / "again")
    first = sorted(p.relative_to(out) for p in out.rglob("*") if p.is_file())
    second = sorted(p.relative_to(again) for p in again.rglob("*") if p.is_file())
    assert first == second
    for path in first:
        assert (out / path).read_bytes() == (again / path).read_bytes(), path


def test_the_client_data_patch_holds_the_client_tables(out):
    import mpyq
    archive = mpyq.MPQArchive(str(out / "client" / "client-data-patch.MPQ"), listfile=True)
    names = sorted(name.decode() for name in archive.files)
    assert names == ["DBFilesClient\\Spell.dbc", "DBFilesClient\\Talent.dbc", "DBFilesClient\\TalentTab.dbc"]
    for name in names:
        assert archive.read_file(name) == (out / "client" / name.replace("\\", "/")).read_bytes(), name


def test_the_talent_rows_are_in_tree_tier_column_order_as_in_the_stock_table(out):
    # The talent window of the 1.12 client takes a tree's talents in row order (GetTalentInfo(tab, i))
    # and draws prerequisite arrows as it goes. The stock Talent.dbc is in this order.
    def order(path):
        data = path.read_bytes()
        records, fields = struct.unpack_from("<4s4I", data, 0)[1:3]
        return [struct.unpack_from("<4I", data, 20 + i * fields * 4)[1:4] for i in range(records)]
    assert order(STOCK / "Talent.dbc") == sorted(order(STOCK / "Talent.dbc"))
    assert order(out / "client" / "DBFilesClient" / "Talent.dbc") == sorted(order(out / "client" / "DBFilesClient" / "Talent.dbc"))
