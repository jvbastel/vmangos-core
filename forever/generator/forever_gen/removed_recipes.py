"""The removal rows of the recipe files (blacksmithing.tsv, #229): stock recipes that Forever took away.

A removal row (kind `removed`) names a stock recipe spell. On the Forever realm the recipe leaves its skill
line and every trainer:
- the client SkillLineAbility.dbc loses the stock rows of the spell in its skill line (the craft window and
  the skill-ups go by them),
- the server removes them with the removal marker (removals.py): forever_skill_line_ability by ID, and
  forever_npc_trainer by each learn spell of the recipe.
The spell itself stays in Spell.dbc and spell_template, so that a character who knows it keeps its row
(#233 decides what that character gets). The plans item is only recorded.
"""
from collections import namedtuple

from . import removals
from .spec import SpecError

KIND = "removed"
NO_VALUE = "-"
# The columns of a removal row that hold a value; the other recipe columns are NO_VALUE.
USED = ("kind", "spell", "sourceSpell", "name", "skillLine", "plans", "foreverSpell", "notes")
# plans: the stock plans item of the recipe, or None.
RemovedRecipe = namedtuple("RemovedRecipe", "spell source_spell name skill_line plans notes")


def read_row(where, raw):
    unused = [column for column, value in raw.items() if column not in USED and value != NO_VALUE]
    if unused:
        raise SpecError("%s: a removal row has no %s (write %s)" % (where, ", ".join(unused), NO_VALUE))
    return RemovedRecipe(spell=int(raw["spell"]), source_spell=int(raw["sourceSpell"]), name=raw["name"],
                         skill_line=int(raw["skillLine"]),
                         plans=None if raw["plans"] == NO_VALUE else int(raw["plans"]), notes=raw["notes"])


def check(spec, stock):
    """A removal row names a stock recipe spell (spell = sourceSpell) with a stock skill line ability in its
    skill line, once, and no other recipe row of the spec names that spell."""
    from .stock_recipes import stock_abilities
    stock_spells = stock.spell.by_id()
    others = {recipe.spell for recipe in list(spec.recipes) + list(spec.stock_recipes)}
    seen = set()
    for recipe in spec.removed_recipes:
        where = "removed recipe %s: spell %d" % (recipe.name, recipe.spell)
        if recipe.source_spell != recipe.spell:
            raise SpecError("%s: a removal row names the stock spell twice (sourceSpell %d)" % (where, recipe.source_spell))
        if recipe.spell in seen:
            raise SpecError("%s is in the spec twice" % where)
        seen.add(recipe.spell)
        if recipe.spell not in stock_spells:
            raise SpecError("%s is no stock spell" % where)
        if recipe.spell in others:
            raise SpecError("%s is also a recipe row of the spec" % where)
        if not stock_abilities(recipe, stock):
            raise SpecError("%s has no stock skill line ability in skill line %d" % (where, recipe.skill_line))


def apply(table, spec, stock):
    """The client SkillLineAbility.dbc `table` without the stock rows of the removed recipes."""
    from .stock_recipes import stock_abilities
    gone = {row[0] for recipe in spec.removed_recipes for row in stock_abilities(recipe, stock)}
    table.rows = [row for row in table.rows if row[0] not in gone]
    return table


def server_sql(spec, stock):
    """The removal lines of the removed recipes: their stock skill line abilities and the trainer rows of
    their learn spells. build.server_sql writes them after removals.marker_sql."""
    from .stock_recipes import learn_spells, stock_abilities
    lines = []
    for recipe in sorted(spec.removed_recipes, key=lambda r: r.spell):
        lines += [""]
        lines += removals.remove_rows("skill_line_ability", "id", [row[0] for row in stock_abilities(recipe, stock)],
                                      "Removed recipe %s (%d): its skill line ability." % (recipe.name, recipe.spell))
        lines += removals.remove_rows("npc_trainer", "spell", learn_spells(recipe, stock),
                                      "Removed recipe %s (%d): the trainer rows of its learn spells." % (recipe.name, recipe.spell))
    return lines
