"""The data pipeline test of the Forever rules (spec jvbastel/WOWAI#204, testing decision 1).

It runs the generator on the spec folder forever/spec, as the test realm kit does, and reads what it wrote from the
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

from forever_gen import client_patch

ROOT = Path(__file__).resolve().parents[3]
# The spec folder (one file per area, #226) and its Warrior file, whose rows these tests check.
SPEC_DIR = ROOT / "forever" / "spec"
SPEC = SPEC_DIR / "warrior.tsv"
GENERATOR = ROOT / "forever" / "generator" / "generate.py"
EXPORTER = ROOT / "bots" / "tools" / "talents" / "export_talents.py"
STOCK = Path(os.environ.get("FOREVER_STOCK_DBC", "/mnt/c/Games/vmangos-deploy/storage/mangosd/extracted-data/5875/dbc"))

pytestmark = pytest.mark.skipif(not (STOCK / "Spell.dbc").is_file(), reason="no stock DBC folder at %s" % STOCK)

# Spell.dbc 1.12.1: 0 ID, 117 SpellIconID, 120 Name (enUS), 129 Rank (enUS), 138 Description (enUS).
SPELL_ID, SPELL_ICON, SPELL_NAME, SPELL_RANK, SPELL_DESCRIPTION = 0, 117, 120, 129, 138


def spec_rows(kind=None):
    lines = [line for line in SPEC.read_text(encoding="utf-8").splitlines() if line and not line.startswith("#")]
    header = lines[0].split("\t")
    rows = [dict(zip(header, line.split("\t"))) for line in lines[1:]]
    return [row for row in rows if kind is None or row["kind"] == kind]


def spells_of(row):
    return [int(s) for s in row["spells"].split(",")]


def set_values(row, name):
    """The values per rank of a `set` column entry of a spec row, or None."""
    for item in row["set"].split(";"):
        key, _, value = item.partition("=")
        if key == name:
            values = value.split("/")
            return values * int(row["ranks"]) if len(values) == 1 else values
    return None


def int32(value):
    return value - (1 << 32) if value >= 1 << 31 else value


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


def generate(out, spec=SPEC_DIR):
    subprocess.run([sys.executable, "-I", str(GENERATOR), "--spec", str(spec), "--stock", str(STOCK),
                    "--out", str(out)], check=True)
    return out


def export(dbc_folder, tmp_path):
    names = tmp_path / "names.tsv"
    names.write_text("".join("%s\t%s\n" % (row["spells"].split(",")[0], row["name"]) for row in spec_rows("talent")))
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
        values = dict(re.findall(r"`(\w+)`=('(?:[^'\\]|\\.)*'|-?\d+(?:\.\d+)?)", m.group(1)))
        spells[int(values["entry"])] = {k: v.strip("'") for k, v in values.items()}
    return spells


def test_the_exported_warrior_trees_are_the_spec_tree(out, tmp_path):
    warrior = sorted(row for row in export(out / "dbc", tmp_path) if row[1] == "warrior")
    expected = sorted([r["talentId"], "warrior", r["tree"], r["tier"], r["column"], r["ranks"],
                       r["requiredTalent"], r["requiredRank"], r["name"]] for r in spec_rows("talent"))
    assert warrior == expected


def test_the_other_classes_keep_their_stock_trees(out, tmp_path):
    forever = [row[:8] for row in export(out / "dbc", tmp_path) if row[1] != "warrior"]
    stock = [row[:8] for row in export(STOCK, tmp_path) if row[1] != "warrior"]
    assert len(stock) > 300
    assert forever == stock


def test_the_client_gets_the_same_talent_tables_as_the_server(out):
    for name in ("Talent.dbc", "TalentTab.dbc"):
        assert (out / "client" / "DBFilesClient" / name).read_bytes() == (out / "dbc" / name).read_bytes()


def expected_rank_text(row, rank, stock_rank_text):
    """The rank text of the spellbook: the `set` value, else "Rank N" for a talent or spell with more than
    one rank, else the stock text of a kept stock spell, else none."""
    named = set_values(row, "nameSubtext")
    if named:
        return named[rank]
    if int(row["ranks"]) > 1:
        return "Rank %d" % (rank + 1)
    return stock_rank_text


def test_each_spec_spell_is_in_the_client_and_the_server_with_its_rank_and_icon(out):
    client, text = read_dbc(out / "client" / "DBFilesClient" / "Spell.dbc")
    stock, stock_text = read_dbc(STOCK / "Spell.dbc")
    server = server_spells(out)
    for row in spec_rows():
        for rank, spell in enumerate(spells_of(row)):
            rank_text = expected_rank_text(row, rank, stock_text(stock[spell][SPELL_RANK]) if spell in stock else "")
            assert spell in client, spell
            assert text(client[spell][SPELL_NAME]) == row["name"], spell
            assert text(client[spell][SPELL_RANK]) == rank_text, spell
            assert client[spell][SPELL_ICON] == int(row["icon"]), spell
            assert spell in server, spell
            assert server[spell]["name"] == row["name"], spell
            assert server[spell]["nameSubtext"] == rank_text, spell
            assert int(server[spell]["spellIconId"]) == int(row["icon"]), spell


def test_a_rank_has_the_spec_value_and_tooltip(out):
    # Improved Rend rank 2 of build 70450: 23% (stock 25%). It keeps its stock spell 12658. A DBC holds a
    # value as base points + 1 (die sides 1).
    client, text = read_dbc(out / "client" / "DBFilesClient" / "Spell.dbc")
    server = server_spells(out)
    assert int32(client[12658][76]) + 1 == 23
    assert text(client[12658][SPELL_DESCRIPTION]) == "Increases the Bleed damage done by your Rend ability by $s1%."
    assert int(server[12658]["effectBasePoints1"]) + 1 == 23
    # Improved Execute rank 1: -3 Rage, in tenths.
    assert int32(client[20502][76]) + 1 == -30
    assert int(server[20502]["effectBasePoints1"]) + 1 == -30


def test_a_changed_base_ability_has_its_forever_values(out):
    client, _ = read_dbc(out / "client" / "DBFilesClient" / "Spell.dbc")
    server = server_spells(out)
    # Battle Shout rank 7: 139 attack power (stock 232), 3 min (SpellDuration.dbc 25, stock 2 min).
    assert int32(client[25289][76]) + 1 == 139 and client[25289][30] == 25
    assert int(server[25289]["effectBasePoints1"]) + 1 == 139 and int(server[25289]["durationIndex"]) == 25
    # Shield Wall: 15 min of its own (no shared category).
    assert (client[871][2], client[871][19], client[871][20]) == (0, 900000, 0)
    assert (server[871]["category"], server[871]["recoveryTime"], server[871]["categoryRecoveryTime"]) == ("0", "900000", "0")
    # Revenge rank 1: 20-24 damage, base points 19 plus 1 to 5.
    assert (client[6572][76], client[6572][67], client[6572][64]) == (19, 1, 5)
    assert (server[6572]["effectBasePoints1"], server[6572]["effectBaseDice1"], server[6572]["effectDieSides1"]) == ("19", "1", "5")


def test_battle_shout_reaches_the_raid(out):
    # jvbastel/WOWAI#215: each rank of Battle Shout targets the raid within the caster's range (56) and the
    # caster (1), in place of the stock party within the caster's range (20). The raid target of the core
    # leaves the caster out, so the caster comes from target B.
    client, _ = read_dbc(out / "client" / "DBFilesClient" / "Spell.dbc")
    stock, _ = read_dbc(STOCK / "Spell.dbc")
    server = server_spells(out)
    for spell in (6673, 5242, 6192, 11549, 11550, 11551, 25289):
        assert (stock[spell][82], stock[spell][85]) == (20, 0), spell
        assert (client[spell][82], client[spell][85]) == (56, 1), spell
        assert (server[spell]["effectImplicitTargetA1"], server[spell]["effectImplicitTargetB1"]) == ("56", "1"), spell


def test_a_new_spell_is_a_copy_of_its_source_with_the_spec_values(out):
    client, text = read_dbc(out / "client" / "DBFilesClient" / "Spell.dbc")
    stock, _ = read_dbc(STOCK / "Spell.dbc")
    # Spearing Strike (60100), a copy of Mortal Strike 12294: 40% weapon damage, 15 Rage, 20 sec, Battle Stance.
    spear, mortal = client[60100], stock[12294]
    assert (spear[61], spear[62], spear[91]) == (121, 31, 0)
    assert int32(spear[77]) + 1 == 40
    assert (spear[32], spear[19], spear[2], spear[20], spear[11]) == (150, 20000, 0, 0, 0x10000)
    assert spear[160:163] == mortal[160:163]  # spell family and family flags of the source
    assert int(server_spells(out)[60100]["effect2"]) == 31


def test_the_stock_spell_rows_that_the_spec_does_not_name_stay_in_the_client_table(out):
    client, text = read_dbc(out / "client" / "DBFilesClient" / "Spell.dbc")
    stock, stock_text = read_dbc(STOCK / "Spell.dbc")
    named = {spell for row in spec_rows() for spell in spells_of(row)}
    assert set(stock) <= set(client)
    for spell, row in stock.items():
        if spell not in named:
            assert client[spell][:SPELL_NAME] == row[:SPELL_NAME], spell
            assert text(client[spell][SPELL_NAME]) == stock_text(row[SPELL_NAME]), spell


def test_new_ids_are_in_the_free_range(out):
    low, high = free_range()
    stock_spells, _ = read_dbc(STOCK / "Spell.dbc")
    stock_talents, _ = read_dbc(STOCK / "Talent.dbc")
    new = 0
    for row in spec_rows():
        if row["kind"] == "talent" and int(row["talentId"]) not in stock_talents:
            assert low <= int(row["talentId"]) <= high
        for spell in spells_of(row):
            if spell not in stock_spells:
                assert low <= spell <= high
                new += 1
    assert new > 20


def test_the_spec_has_the_talent_count_of_its_build_and_a_port_type_on_each_row():
    m = re.search(r"^# Talent count of build 1\.60\.1\.70450: (\d+) \(Arms (\d+), Fury (\d+), Protection (\d+)\)",
                  SPEC.read_text(encoding="utf-8"), re.M)
    assert m and [int(v) for v in m.groups()] == [52, 17, 17, 18]
    talents = spec_rows("talent")
    assert len(talents) == 52
    assert [sum(r["tree"] == tree for r in talents) for tree in ("arms", "fury", "protection")] == [17, 17, 18]
    assert all(r["port"] in ("data", "C++", "substitute") for r in spec_rows())
    assert spec_rows("ability") and spec_rows("base")


def test_the_out_of_scope_forever_changes_are_not_in_the_spec():
    names = " ".join(r["name"] for r in spec_rows()).lower()
    for word in ("expertise", "legacy", "talented", "dual specialization", "racial"):
        assert word not in names


def test_two_runs_give_the_same_bytes(out, tmp_path):
    again = generate(tmp_path / "again")
    first = sorted(p.relative_to(out) for p in out.rglob("*") if p.is_file())
    second = sorted(p.relative_to(again) for p in again.rglob("*") if p.is_file())
    assert first == second
    for path in first:
        assert (out / path).read_bytes() == (again / path).read_bytes(), path


def test_the_client_data_patch_holds_the_client_tables(out):
    import mpyq
    archive = mpyq.MPQArchive(str(out / "client" / client_patch.PATCH_NAME), listfile=True)
    names = sorted(name.decode() for name in archive.files)
    assert names == ["DBFilesClient\\SkillLineAbility.dbc", "DBFilesClient\\Spell.dbc", "DBFilesClient\\Talent.dbc",
                     "DBFilesClient\\TalentTab.dbc"]
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


# Spell.dbc 1.12.1: 11 Stances (1 << (form - 1): Battle 0x10000, Defensive 0x20000, Berserker 0x40000).
SPELL_STANCES = 11
BATTLE_STANCE, DEFENSIVE_STANCE = 0x10000, 0x20000


def test_vanguard_charge_is_stock_charge_that_the_client_allows_in_defensive_stance(out):
    # WOWAI#216: Vanguard swaps each Charge rank (100, 6178, 11578) for its Vanguard Charge (60127-60129).
    # The client checks the stance of a spell itself, so the new rows need Battle and Defensive Stance.
    client, text = read_dbc(out / "client" / "DBFilesClient" / "Spell.dbc")
    stock, _ = read_dbc(STOCK / "Spell.dbc")
    server = server_spells(out)
    for rank, (vanguard, charge) in enumerate(((60127, 100), (60128, 6178), (60129, 11578))):
        assert stock[charge][SPELL_STANCES] == BATTLE_STANCE
        row = client[vanguard]
        assert row[SPELL_STANCES] == BATTLE_STANCE | DEFENSIVE_STANCE, vanguard
        assert int(server[vanguard]["stances"]) == BATTLE_STANCE | DEFENSIVE_STANCE, vanguard
        # The rest is the stock rank: effects, rage, range, cooldown, family (so Improved Charge applies).
        assert row[1:SPELL_STANCES] == stock[charge][1:SPELL_STANCES], vanguard
        assert row[SPELL_STANCES + 1:SPELL_NAME] == stock[charge][SPELL_STANCES + 1:SPELL_NAME], vanguard
        assert row[160:163] == stock[charge][160:163], vanguard
        assert text(row[SPELL_NAME]) == "Charge" and text(row[SPELL_RANK]) == "Rank %d" % (rank + 1)


def test_the_vanguard_talent_is_a_passive_spell_that_the_rules_switch_knows(out):
    # The core swaps Charge when a Warrior learns the Vanguard talent spell, and back when it loses it
    # (src/game/Forever/Vanguard.cpp). test_spell_ids.py checks its IDs in ForeverSpellIds.h.
    client, _ = read_dbc(out / "client" / "DBFilesClient" / "Spell.dbc")
    assert client[60119][6] & 0x40  # passive: a talent, no button
    rows = {row["name"]: row for row in spec_rows()}
    assert spells_of(rows["Vanguard"]) == [60119]
    charge = [row for row in spec_rows("ability") if row["name"] == "Charge"][0]
    assert list(zip(spells_of(charge), map(int, charge["sourceSpells"].split(",")))) == \
        [(60127, 100), (60128, 6178), (60129, 11578)]


# --- #218: the Fury talents of the core: test_spell_ids.py checks their IDs (ForeverSpellIds.h) ---


def test_the_fury_buffs_are_visible_dummy_auras_with_their_duration_and_charges(out):
    # Lingering Rage 60131: 10 sec (SpellDuration.dbc 1; the core shortens it to the talent's delay).
    # Gore Drinker 60132: 8 sec (SpellDuration.dbc 31), 3 charges, no proc flags (the core uses the charges).
    client, text = read_dbc(out / "client" / "DBFilesClient" / "Spell.dbc")
    server = server_spells(out)
    for spell, name, duration, charges in ((60131, "Lingering Rage", 1, 0), (60132, "Gore Drinker", 31, 3)):
        row = client[spell]
        assert text(row[SPELL_NAME]) == name
        assert not row[6] & 0x40, spell        # not passive: the player sees the buff
        assert (row[61], row[91], row[82]) == (6, 4, 1), spell     # apply aura DUMMY on the caster
        assert (row[30], row[24], row[26]) == (duration, 0, charges), spell
        assert int(server[spell]["durationIndex"]) == duration, spell


def test_no_new_forever_spell_is_a_channel(out):
    # A Forever buff copied from a channeled source (17189 has attributesEx 0x4 and interrupt flags) is a
    # channel in the core: the next move or cast ends it (Victorious #213, Lingering Rage and Gore Drinker #218).
    ATTRIBUTES_EX, CHANNELED = 7, 0x4
    rows, _ = read_dbc(out / "client" / "DBFilesClient" / "Spell.dbc")
    channels = sorted(i for i, row in rows.items() if i >= 60100 and row[ATTRIBUTES_EX] & CHANNELED)
    assert channels == []
    server = server_spells(out)
    channels = sorted(i for i, row in server.items() if i >= 60100 and int(row.get("attributesEx", 0)) & CHANNELED)
    assert channels == []


def test_the_other_spec_files_leave_the_warrior_output_as_it_is(out, tmp_path):
    # #226: the spec is a folder with one file per area. The Warrior file alone gives the Warrior output;
    # the other files add their own rows and change no Warrior byte: the talent tables are the same, and
    # each row of the Warrior-only spell and skill line ability tables is in the full tables as it is.
    alone = tmp_path / "warrior-only"
    alone.mkdir()
    (alone / "warrior.tsv").write_bytes(SPEC.read_bytes())
    warrior = generate(tmp_path / "warrior-out", alone)
    assert sorted(p.name for p in SPEC_DIR.glob("*.tsv")) != ["warrior.tsv"], "the spec folder has only the Warrior file"
    for name in ("dbc/Talent.dbc", "dbc/TalentTab.dbc", "client/DBFilesClient/Talent.dbc"):
        assert (out / name).read_bytes() == (warrior / name).read_bytes(), name
    for name in ("Spell.dbc", "SkillLineAbility.dbc"):
        full, full_text = read_dbc(out / "client" / "DBFilesClient" / name)
        only, only_text = read_dbc(warrior / "client" / "DBFilesClient" / name)
        assert set(only) < set(full), name
        for key, row in only.items():
            if name == "Spell.dbc":
                assert full[key][:SPELL_NAME] == row[:SPELL_NAME], key
                assert full_text(full[key][SPELL_NAME]) == only_text(row[SPELL_NAME]), key
            else:
                assert full[key] == row, key
    sql = (out / "sql" / "forever_world.sql").read_text(encoding="utf-8")
    for line in (warrior / "sql" / "forever_world.sql").read_text(encoding="utf-8").splitlines():
        if line.startswith(("UPDATE", "INSERT")):
            assert line in sql, line
