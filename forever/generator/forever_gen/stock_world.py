"""The stock world data of the loot rows (#242): the generator has no world database, so it reads the files of
forever_gen/stock_world/, which tools/stock_world_rows.py writes from a stock world database (its docstring names
each file and column).
"""
import csv
import os
from collections import namedtuple

FOLDER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "stock_world")

# A stock weapon or armor item that a new item with no stock twin may copy (loot.py).
Gear = namedtuple("Gear", "entry item_class subclass inventory_type item_level quality display_id disenchant_id")


def _rows(folder, name):
    with open(os.path.join(folder, name), encoding="utf-8", newline="") as handle:
        lines = [line for line in handle if not line.startswith("#")]
    return list(csv.DictReader(lines, delimiter="\t", quoting=csv.QUOTE_NONE))


class StockWorld:
    """creatures {entry: (loot ID, name)}, quests {entry: (title, [the six reward choices])}, references
    {entry: [the creature loot entries that use it]}, gear [Gear]."""

    def __init__(self, creatures, quests, references, gear):
        self.creatures, self.quests, self.references, self.gear = creatures, quests, references, gear

    @classmethod
    def read(cls, folder=FOLDER):
        creatures = {int(r["entry"]): (int(r["loot_id"]), r["name"]) for r in _rows(folder, "creatures.tsv")}
        quests = {int(r["entry"]): (r["title"], [int(c) for c in r["choices"].split(",")])
                  for r in _rows(folder, "quests.tsv")}
        references = {int(r["entry"]): [int(u) for u in r["users"].split(",") if u] for r in _rows(folder, "references.tsv")}
        gear = [Gear(*(int(r[c]) for c in ("entry", "class", "subclass", "inventory_type", "item_level", "quality",
                                           "display_id", "disenchant_id"))) for r in _rows(folder, "gear.tsv")]
        return cls(creatures, quests, references, gear)
