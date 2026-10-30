"""The source rule of US35 (spec #225) for the rows that are not new: every row names where its data comes
from, so that bad data never reaches a realm.

- a new recipe (foreverSpell) or a new item (foreverItem): new_recipes.check;
- a changed stock recipe: foreverSpell, the Forever spell it comes from;
- a removal row: its notes, why Forever removed it (the source is the Forever client without the recipe);
- an item set: its notes, the Forever item set (or the stock set it changes); a set bonus: foreverSpell;
- a mining drop: its rate column (mining.py: spec or fallback); an ore is a new item.
"""
from .common import NO_VALUE, SpecError


def _has_text(value):
    return bool(value and value.strip() and value != NO_VALUE)


def check(spec):
    for recipe in spec.stock_recipes:
        if recipe.forever_spell is None:
            raise SpecError("stock recipe %s: spell %d has no source (foreverSpell)" % (recipe.name, recipe.spell))
    for recipe in spec.removed_recipes:
        if not _has_text(recipe.notes):
            raise SpecError("removed recipe %s: spell %d has no source (notes: why Forever removed it)"
                            % (recipe.name, recipe.spell))
    for item_set in spec.item_sets:
        if not _has_text(item_set.notes):
            raise SpecError("item set %s (%d) has no source (notes: its Forever item set)" % (item_set.name, item_set.set_id))
