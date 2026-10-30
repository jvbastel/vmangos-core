"""The dead-content rule of the new profession rows (#230): the generator leaves out every new recipe and new
item with no remaining use, and lists them in the cut list for review.

The spec keeps every new recipe and item that the Forever client can read, also those that cannot work on a
1.12 realm. The rule cuts:
- a recipe that needs a Forever-only station (FOREVER_STATIONS): the Master Forge and the Molten Foundry
  are camping objects that 1.12 does not have (the explicit exclusions of #230);
- a recipe that needs a new item that only cut recipes make (Heavy Thorium Bar, made only by Smelt Heavy
  Thorium at the Molten Foundry);
- a recipe that makes a reagent (a new item that some recipe of the spec uses) that no remaining recipe uses
  (a smelt whose bar only cut recipes need);
- a new item that only cut recipes make, a raw new reagent (no recipe makes it: an ore, a drop) that no
  remaining recipe uses, and the plans of a cut recipe.
It repeats until nothing more is cut. Stock items, changed stock items and new items that no recipe names
are not touched. A stock recipe row is never cut, so a new reagent that it uses stays.
"""
from collections import namedtuple

from . import items as item_rows

# The spell focus of a recipe that needs a Forever camping object, which 1.12 does not have.
FOREVER_STATIONS = ("Master Forge", "Molten Foundry")

# kind: recipe or item; id: the spell ID or the item entry; reason: why the rule cuts it.
Cut = namedtuple("Cut", "kind id name reason")


def _recipe_reason(recipe, cut, new_items, makers, users):
    """Why the rule cuts a recipe, or None."""
    if recipe.focus in FOREVER_STATIONS:
        return "needs a %s" % recipe.focus
    for reagent, _ in recipe.reagents:
        if reagent in new_items and reagent in makers and all(m.spell in cut for m in makers[reagent]):
            return "needs %s (%d), which no remaining recipe makes" % (new_items[reagent].name, reagent)
    made = recipe.creates[0]
    if made in new_items and made in users and all(u.spell in cut for u in users[made]):
        return "makes %s (%d), which no remaining recipe uses" % (new_items[made].name, made)
    return None


def _item_reason(entry, cut, teaches, makers, users):
    """Why the rule cuts a new item that a recipe names, or None."""
    if entry in teaches:
        return "teaches a cut recipe" if teaches[entry].spell in cut else None
    if entry in makers:
        return "no remaining recipe makes it" if all(m.spell in cut for m in makers[entry]) else None
    return "no remaining recipe uses it" if all(u.spell in cut for u in users.get(entry, ())) else None


def cut(spec):
    """(the spec without the cut rows, [Cut] sorted by kind and ID)."""
    new_items = {item.entry: item for item in spec.items if item.kind == item_rows.NEW}
    makers, users, teaches = {}, {}, {}
    for recipe in spec.recipes:
        makers.setdefault(recipe.creates[0], []).append(recipe)
        for reagent, _ in recipe.reagents:
            users.setdefault(reagent, []).append(recipe)
        if recipe.plans is not None:
            teaches[recipe.plans] = recipe
    # A stock row (#228) is never cut: a new reagent that it uses stays in use.
    for recipe in spec.stock_recipes:
        for reagent, _ in recipe.reagents:
            users.setdefault(reagent, []).append(recipe)
    cut_recipes = {}
    changed = True
    while changed:
        changed = False
        for recipe in spec.recipes:
            if recipe.spell in cut_recipes:
                continue
            reason = _recipe_reason(recipe, cut_recipes, new_items, makers, users)
            if reason:
                cut_recipes[recipe.spell] = reason
                changed = True
    named = set(makers) | set(users) | set(teaches)
    cut_items = {}
    for entry in sorted(named & set(new_items)):
        reason = _item_reason(entry, cut_recipes, teaches, makers, users)
        if reason:
            cut_items[entry] = reason
    cuts = [Cut("item", entry, new_items[entry].name, reason) for entry, reason in cut_items.items()]
    cuts += [Cut("recipe", r.spell, r.name, cut_recipes[r.spell]) for r in spec.recipes if r.spell in cut_recipes]
    live = spec._replace(recipes=[r for r in spec.recipes if r.spell not in cut_recipes],
                         items=[i for i in spec.items if i.entry not in cut_items])
    return live, sorted(cuts, key=lambda c: (c.kind, c.id))


def cut_list_text(cuts):
    """The cut list as a TSV file: a comment line, the column line and one line per cut row."""
    lines = ["# Dead-content cut list (forever/generator, #230): %d rows that the generator leaves out." % len(cuts),
             "kind\tid\tname\treason"]
    lines += ["%s\t%d\t%s\t%s" % cut for cut in cuts]
    return "\n".join(lines) + "\n"
