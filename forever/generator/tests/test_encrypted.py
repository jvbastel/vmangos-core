"""The rows in encrypted client data (US42 of spec jvbastel/WOWAI#225): left out and listed.

Build 1.60.1.70685 of the Forever client hides 148 profession items in ItemSparse/Item sections whose key is
unknown. forever/spec/encrypted.tsv names them. The generator refuses a spec row that takes its data from one
of them, and writes them into encrypted-list.tsv next to the cut list, so that the post-launch refresh knows
what to try again. The test runs the generator on the spec folder and reads what it wrote.
"""
import pytest

from test_blacksmithing import refused
from test_pipeline import STOCK, generate

pytestmark = pytest.mark.skipif(not (STOCK / "SkillLineAbility.dbc").is_file(), reason="no stock DBC folder at %s" % STOCK)

# The 15 plans of new recipes (blacksmithing.tsv, "Excluded (encrypted ...)") and the 4 stock crafted items
# (items.tsv: Rune Edge, Ironvine Breastplate, Gloves and Belt).
NEW_RECIPE_PLANS = [251340] + list(range(251342, 251355)) + [251395]
STOCK_ITEMS = [12779, 22762, 22763, 22764]


@pytest.fixture(scope="module")
def listed(tmp_path_factory):
    out = generate(tmp_path_factory.mktemp("forever-encrypted"))
    lines = (out / "encrypted-list.tsv").read_text(encoding="utf-8").splitlines()
    rows = [line.split("\t") for line in lines if not line.startswith("#")]
    assert rows[0] == ["foreverItem", "name", "used"]
    return {int(row[0]): row for row in rows[1:]}


def test_the_generator_lists_the_148_encrypted_rows(listed):
    assert len(listed) == 148


def test_the_list_holds_the_plans_of_new_recipes_and_the_stock_items_that_were_left_out(listed):
    assert set(NEW_RECIPE_PLANS + STOCK_ITEMS) <= set(listed)
    assert listed[12779][1] == "Rune Edge"
    assert all("plans:fv-new" in listed[plans][2] for plans in NEW_RECIPE_PLANS)


def test_the_list_is_sorted_by_item(listed):
    assert list(listed) == sorted(listed)


def test_an_item_row_from_an_encrypted_client_item_is_refused(tmp_path):
    # The Copper Mace row with its Forever data taken from Rune Edge (12779), which the client cannot read.
    error = refused(tmp_path, "items.tsv", ("\t2861\t106\t2844\t", "\t2861\t106\t12779\t"))
    assert "Copper Mace" in error and "foreverItem 12779 is in encrypted client data (encrypted.tsv)" in error


def test_an_encrypted_row_named_twice_is_refused(tmp_path):
    error = refused(tmp_path, "encrypted.tsv", ("12779\tRune Edge\t", "12779\tRune Edge\t"),
                    ("22762\t", "12779\t"))
    assert "12779 is in encrypted.tsv twice" in error
