"""The Forever baseline abilities and base ability changes of jvbastel/WOWAI#212 in the generated data.

A baseline ability is learned at its level with no trainer visit (the core reads forever_baseline_spell).
A new ability gets a skill line ability row, in the server table and in the client data patch, so that
the spellbook shows it in its tab and a new rank joins the spell chain of its ability.
"""
import re

import pytest

from test_pipeline import STOCK, generate, int32, read_dbc, server_spells

pytestmark = pytest.mark.skipif(not (STOCK / "Spell.dbc").is_file(), reason="no stock DBC folder at %s" % STOCK)

VICTORY_RUSH, TACTICAL_MASTERY, SLAM_1, BERSERKER_RAGE = 60123, 60125, 60126, 18499
SLAM_2 = 1464
VANGUARD_CHARGE = (60127, 60128, 60129)
ARMS, FURY = 26, 256
# SkillLineAbility.dbc 1.12.1: 0 ID, 1 SkillLine, 2 Spell, 3 RaceMask, 4 ClassMask, 8 SupersededBy.
SLA_SKILL, SLA_SPELL, SLA_CLASS, SLA_SUPERSEDED = 1, 2, 4, 8
WARRIOR_MASK = 1


@pytest.fixture(scope="module")
def out(tmp_path_factory):
    return generate(tmp_path_factory.mktemp("forever212"))


def sql(out):
    return (out / "sql" / "forever_world.sql").read_text(encoding="utf-8")


def baseline_spells(out):
    m = re.search(r"^INSERT INTO `forever_baseline_spell` \(`spell_id`\) VALUES (.*);$", sql(out), re.M)
    return sorted(int(v) for v in re.findall(r"\((\d+)\)", m.group(1))) if m else []


def server_abilities(out):
    """spell -> the columns that the generated SQL sets on its forever_skill_line_ability row."""
    rows = {}
    for m in re.finditer(r"^UPDATE `forever_sla_new` SET (.*);$", sql(out), re.M):
        values = {k: int(v) for k, v in re.findall(r"`(\w+)`=(\d+)", m.group(1))}
        rows[values["spell_id"]] = values
    return rows


def client_abilities(out):
    rows, _ = read_dbc(out / "client" / "DBFilesClient" / "SkillLineAbility.dbc")
    return {row[SLA_SPELL]: row for row in rows.values()}


def test_the_baseline_abilities_are_listed_for_the_core(out):
    # Victory Rush (20), Tactical Mastery (14), Slam rank 1 (20) and Berserker Rage (30, stock trainer 32).
    assert baseline_spells(out) == sorted([VICTORY_RUSH, TACTICAL_MASTERY, SLAM_1, BERSERKER_RAGE])


def test_the_baseline_table_is_made_again_at_each_load(out):
    text = sql(out)
    assert "DROP TABLE IF EXISTS `forever_baseline_spell`;" in text
    assert re.search(r"CREATE TABLE `forever_baseline_spell` \(`spell_id` SMALLINT UNSIGNED NOT NULL", text)


def test_a_new_ability_has_a_skill_line_ability_in_the_server_and_the_client(out):
    server, client = server_abilities(out), client_abilities(out)
    for spell, skill in ((VICTORY_RUSH, FURY), (TACTICAL_MASTERY, ARMS), (SLAM_1, FURY)):
        assert server[spell]["skill_id"] == skill and server[spell]["class_mask"] == WARRIOR_MASK, spell
        assert server[spell]["learn_on_get_skill"] == 0, spell  # the core learns it at its level, not with the skill
        assert client[spell][SLA_SKILL] == skill and client[spell][SLA_CLASS] == WARRIOR_MASK, spell
        assert server[spell]["id"] == client[spell][0] == spell, spell


def test_slam_rank_1_comes_before_the_stock_ranks(out):
    # The stock Slam 1464 becomes rank 2: the skill line ability of rank 1 names it as the next rank, so
    # that the core chains them (SpellMgr::LoadSpellChains) and the trainer wants rank 1 first.
    assert server_abilities(out)[SLAM_1]["superseded_by_spell"] == SLAM_2
    assert client_abilities(out)[SLAM_1][SLA_SUPERSEDED] == SLAM_2


def test_the_client_data_patch_holds_the_skill_line_abilities(out):
    stock, _ = read_dbc(STOCK / "SkillLineAbility.dbc")
    client, _ = read_dbc(out / "client" / "DBFilesClient" / "SkillLineAbility.dbc")
    assert set(stock) < set(client)
    assert all(client[i] == row for i, row in stock.items())


def test_tactical_mastery_keeps_10_rage_through_the_stance_class_script(out):
    # SPELL_AURA_OVERRIDE_CLASS_SCRIPTS (112) with script 832: the stance change keeps 10 Rage (SpellAuras.cpp).
    client, _ = read_dbc(out / "client" / "DBFilesClient" / "Spell.dbc")
    server = server_spells(out)[TACTICAL_MASTERY]
    assert (client[TACTICAL_MASTERY][91], client[TACTICAL_MASTERY][106]) == (112, 832)
    assert (server["effectApplyAuraName1"], server["effectMiscValue1"]) == ("112", "832")


def test_every_spellbook_ability_is_shown_in_the_spellbook(out):
    # SPELL_ATTR_DO_NOT_DISPLAY (0x80) hides a spell from the client spellbook. Tactical Mastery is a copy
    # of a hidden passive aura (16462), so it was learned but not shown. A server row without its own
    # attributes keeps those of its source spell, as the client row does.
    client, _ = read_dbc(out / "client" / "DBFilesClient" / "Spell.dbc")
    server = server_spells(out)
    for spell in client_abilities(out):
        if spell >= 60000:
            server_attributes = int(server[spell].get("attributes", client[spell][6]))
            assert not client[spell][6] & 0x80 and not server_attributes & 0x80, spell


def test_tactical_mastery_is_a_passive_like_parry(out):
    # Parry (3127) and Dual Wield (674): ability 0x10 and passive 0x40, shown in the spellbook as Passive.
    client, _ = read_dbc(out / "client" / "DBFilesClient" / "Spell.dbc")
    assert client[TACTICAL_MASTERY][6] == client[3127][6] == 0x50


def test_slam_is_not_pushed_back(out):
    # Stock interrupt flags 0xF; 0x2 is the damage pushback.
    client, _ = read_dbc(out / "client" / "DBFilesClient" / "Spell.dbc")
    server = server_spells(out)
    for spell in (SLAM_1, SLAM_2, 8820, 11604, 11605):
        assert client[spell][21] == 0xD and server[spell]["interruptFlags"] == "13", spell


def test_every_slam_rank_shares_one_18_sec_cooldown(out):
    # Client 70685: Slam ranks 1-5 share category 2412 with an 18 sec category cooldown. A 1.12 player keeps
    # the lower ranks in the spellbook, so a cooldown of each rank alone would let rank 1 follow rank 5.
    client, _ = read_dbc(out / "client" / "DBFilesClient" / "Spell.dbc")
    stock, _ = read_dbc(STOCK / "Spell.dbc")
    ranks = (SLAM_1, SLAM_2, 8820, 11604, 11605)
    categories = {client[s][2] for s in ranks}
    assert len(categories) == 1 and not categories & {row[2] for row in stock.values()}
    assert [(client[s][19], client[s][20]) for s in ranks] == [(0, 18000)] * 5


def test_bloodrage_has_no_combat_script(out):
    # The stock script spell_warrior_bloodrage only puts the Warrior in combat.
    assert server_spells(out)[2687]["script_name"] == ""


def test_whirlwind_hits_with_the_off_hand_too(out):
    # Whirlwind triggers an off-hand copy (60130): it needs an off-hand weapon (attributes ex3 0x01000000),
    # costs nothing and has no cooldown of its own.
    client, _ = read_dbc(out / "client" / "DBFilesClient" / "Spell.dbc")
    server = server_spells(out)
    whirlwind, off_hand = client[1680], client[60130]
    assert (whirlwind[62], whirlwind[83], whirlwind[110]) == (64, 1, 60130)
    assert (server[1680]["effect2"], server[1680]["effectTriggerSpell2"]) == ("64", "60130")
    assert off_hand[61] == 121 and off_hand[9] & 0x01000000
    assert (off_hand[32], off_hand[19]) == (0, 0)


def test_bloodthirst_ranks_2_to_4_give_movement_speed(out):
    client, _ = read_dbc(out / "client" / "DBFilesClient" / "Spell.dbc")
    for spell, base in ((23892, 37), (23893, 43), (23894, 48)):
        row = client[spell]
        assert int32(row[76]) + 1 == base and int32(row[77]) + 1 == 45, spell
        assert (row[63], row[93], int32(row[78]) + 1) == (6, 31, 10), spell


def test_vanguard_charge_is_in_the_arms_spellbook_tab_with_no_chain(out):
    # The stock Charge ranks are in skill line 26 (Arms). Vanguard Charge (#216) is one rank at a time
    # (Vanguard.cpp swaps it), so its skill line abilities name no next rank.
    server, client = server_abilities(out), client_abilities(out)
    for spell in VANGUARD_CHARGE:
        assert server[spell]["skill_id"] == ARMS and server[spell]["superseded_by_spell"] == 0, spell
        assert client[spell][SLA_SKILL] == ARMS and client[spell][SLA_SUPERSEDED] == 0, spell
        assert client[spell][SLA_CLASS] == WARRIOR_MASK, spell
    assert not set(VANGUARD_CHARGE) & set(baseline_spells(out))
