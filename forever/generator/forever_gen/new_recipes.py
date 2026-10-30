"""The new recipes of the profession files and their items (#230): the refusals over items and sources, and
the plans that teach a new recipe.

A new recipe's plans are an item row of the spec (a copy of a stock plans item). In 1.12 a plans item teaches
its recipe with an on-use learn spell (Effect1 Learn Spell, EffectTriggerSpell1 = the recipe), for example
2760 "Plans: Runed Copper Breastplate" for 2881. So each new recipe with plans has a learn spell of its own,
a copy of LEARN_SOURCE from the profession spell range, named in the plans column ("<item>/<learn spell>").
The plans item gets the learn spell and the skill that learning the recipe needs (the recipe's minRank).

The refusals run on the whole spec, before the dead-content rule (dead_content.py) cuts rows: every reagent
and made item is a stock item or an item row of the spec (the reagents of the stock rows too), every new
row names its Forever source, and every new item copies a stock item.
"""
import os
from collections import namedtuple

from . import items
from .common import SpecError, id_range

# The stock learn spell that each learn spell copies: Plans: Runed Copper Breastplate (Effect1 Learn Spell).
LEARN_SOURCE = 2760
# item_template spelltrigger 0: on use. spellcharges -1: the plans are used up (as on the stock plans).
ON_USE, USED_UP = 0, -1
# The stock item IDs of the world database (one "<low>-<high>" range or ID per line, from the item_template of
# build 5875; forever/generator/tools/new_recipe_rows.py writes it). The 1.12 client has no item table, and
# the generator reads no world database.
STOCK_ITEM_IDS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "stock_item_ids.txt")

# One learn spell: its ID, the recipe it teaches, and the name and description of its spell.
Learn = namedtuple("Learn", "spell recipe name description")


class ItemIds:
    """The stock item IDs, as ranges."""

    def __init__(self, ranges):
        self.ranges = ranges

    @classmethod
    def read(cls, path=STOCK_ITEM_IDS):
        ranges = []
        with open(path, encoding="utf-8") as handle:
            for line in handle:
                if line.strip() and not line.startswith("#"):
                    low, _, high = line.strip().partition("-")
                    ranges.append((int(low), int(high or low)))
        return cls(ranges)

    def __contains__(self, entry):
        return any(low <= entry <= high for low, high in self.ranges)


def ranges_text(ids):
    """The text of a stock item ID file for these IDs."""
    lines = ["# The stock 1.12 item IDs (item_template, build 5875): one ID or '<low>-<high>' range per line.",
             "# Written by forever/generator/tools/new_recipe_rows.py --stock-item-ids. Never edit by hand."]
    ids = sorted(set(ids))
    start = prev = ids[0]
    for entry in ids[1:] + [None]:
        if entry is not None and entry == prev + 1:
            prev = entry
            continue
        lines.append(str(start) if start == prev else "%d-%d" % (start, prev))
        start = prev = entry
    return "\n".join(lines) + "\n"


def _check_reagents(recipe, item_entries, stock_items):
    """Refuses a reagent of a new or a stock recipe row that is no stock item and no item row of the spec."""
    for reagent, _ in recipe.reagents:
        if reagent not in item_entries and reagent not in stock_items:
            raise SpecError("recipe %s: spell %d: the reagent %d is no stock item and no item row of the spec"
                            % (recipe.name, recipe.spell, reagent))


def check(spec, stock, used_spells):
    """The rules of the new rows over the whole spec: sources, existing items, plans and learn spells.
    `used_spells`: the spell IDs of the spell rows."""
    stock_items = stock.item_ids
    item_entries = {item.entry for item in spec.items}
    for item in spec.items:
        where = "item %s: entry %d" % (item.name, item.entry)
        if item.forever_item is None:
            raise SpecError("%s has no source (foreverItem)" % where)
        if item.source_item not in stock_items:
            raise SpecError("%s: the source item %d is no stock item" % (where, item.source_item))
    low, high = id_range(spec.profession_id_range)
    stock_spells = stock.spell.by_id()
    seen = set(used_spells) | {recipe.spell for recipe in spec.recipes}
    for recipe in spec.stock_recipes:
        _check_reagents(recipe, item_entries, stock_items)
    for recipe in spec.recipes:
        where = "recipe %s: spell %d" % (recipe.name, recipe.spell)
        if recipe.forever_spell is None:
            raise SpecError("%s has no source (foreverSpell)" % where)
        _check_reagents(recipe, item_entries, stock_items)
        made = recipe.creates[0]
        if made not in item_entries and made not in stock_items:
            raise SpecError("%s: the made item %d is no stock item and no item row of the spec" % (where, made))
        if recipe.plans is None:
            continue
        if recipe.plans_spell is None:
            raise SpecError("%s: the plans %d of a new recipe need a learn spell ('<item>/<learn spell>')" % (where, recipe.plans))
        if recipe.plans not in item_entries:
            raise SpecError("%s: the plans %d are no item row of the spec" % (where, recipe.plans))
        if not low <= recipe.plans_spell <= high:
            raise SpecError("%s: the learn spell %d is outside the free profession spell ID range %d-%d"
                            % (where, recipe.plans_spell, low, high))
        if recipe.plans_spell in seen or recipe.plans_spell in stock_spells:
            raise SpecError("%s: the learn spell %d clashes with another spell of the same ID" % (where, recipe.plans_spell))
        seen.add(recipe.plans_spell)


def plans_learns(spec):
    """The Learn of each new recipe with plans and a learn spell, by spell ID. The spell has the name of the
    plans item, as the stock learn spells have."""
    names = {item.entry: item.name for item in spec.items}
    learns = []
    for recipe in spec.recipes:
        if recipe.plans is None or recipe.plans_spell is None:
            continue
        made = names.get(recipe.creates[0], recipe.name)
        learns.append(Learn(recipe.plans_spell, recipe.spell, names[recipe.plans],
                            "Teaches you how to make %s." % made))
    return sorted(learns)


def server_sql(spec):
    """The plans of each new recipe: the learn spell on use (used up) and the skill that it needs, on the
    forever_item_template copy that items.server_sql wrote."""
    lines = []
    for recipe in sorted(spec.recipes, key=lambda r: r.spell):
        if recipe.plans is None or recipe.plans_spell is None:
            continue
        values = [("spellid_1", recipe.plans_spell), ("spelltrigger_1", ON_USE), ("spellcharges_1", USED_UP),
                  ("spellppmrate_1", 0), ("spellcooldown_1", -1), ("spellcategory_1", 0),
                  ("spellcategorycooldown_1", -1), ("required_skill", recipe.skill_line),
                  ("required_skill_rank", recipe.min_rank)]
        lines += ["", "-- The plans of recipe %s (%d) teach it with spell %d." % (recipe.name, recipe.spell, recipe.plans_spell),
                  "UPDATE `%s` SET %s WHERE `entry`=%d;" % (
                      items.TABLE, ", ".join("`%s`=%d" % kv for kv in values), recipe.plans)]
    return lines
