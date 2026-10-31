"""The removal rows of the recipe files (blacksmithing.tsv, #229): stock recipes that Forever took away.

A removal row (kind `removed`) names a stock recipe spell. On the Forever realm the recipe leaves its skill
line and every trainer:
- the client SkillLineAbility.dbc loses the stock rows of the spell in its skill line (the craft window and
  the skill-ups go by them),
- the server removes them with the removal marker (removals.py): forever_skill_line_ability by ID, and
  forever_npc_trainer by each learn spell of the recipe.
The spell itself stays in Spell.dbc and spell_template, so that a character who knows it keeps its row
(#233 decides what that character gets).
All six come from plans. The plans stay an item, so that a character who holds them keeps them, but on the
Forever realm they teach nothing: forever_item_template holds a copy of the stock plans row with the learn
spell taken out of its spell slots, so the plans have no use. The loot and vendor rows that offer them stay
(no Forever creature loot or vendor table yet).
"""
from collections import namedtuple

from . import removals
from .common import NO_VALUE, SpecError, fail_unless
from .items import MAX_ITEM_SPELLS, SPELL_SLOT, TABLE as ITEM_TABLE
from .stock_recipes import no_trainer_template_sql, stock_abilities, stock_learn_spells

KIND = "removed"
# The columns of a removal row that hold a value; the other recipe columns are NO_VALUE.
USED = ("kind", "spell", "sourceSpell", "name", "skillLine", "plans", "notes")
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
    skill line, once, and no other recipe row of the spec names that spell. Its plans, if any, have a learn
    spell and are no other row of the spec."""
    stock_spells = stock.spell.by_id()
    others = {recipe.spell for recipe in list(spec.recipes) + list(spec.stock_recipes)}
    item_rows = {item.entry for item in spec.items}
    other_plans = {recipe.plans for recipe in spec.stock_recipes if recipe.plans is not None}
    seen, plans = set(), set()
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
        if recipe.plans is None:
            continue
        if not stock_learn_spells(recipe.spell, stock):
            raise SpecError("%s: no stock spell learns it, so its plans %d cannot be emptied" % (where, recipe.plans))
        # The generator writes a forever_item_template copy of the plans (server_sql): no other row may.
        if recipe.plans in item_rows:
            raise SpecError("%s: its plans %d are also an item row of the spec" % (where, recipe.plans))
        if recipe.plans in other_plans or recipe.plans in plans:
            raise SpecError("%s: the plans %d are named twice" % (where, recipe.plans))
        plans.add(recipe.plans)


def apply(table, spec, stock):
    """The client SkillLineAbility.dbc `table` without the stock rows of the removed recipes."""
    gone = {row[0] for recipe in spec.removed_recipes for row in stock_abilities(recipe, stock)}
    table.rows = [row for row in table.rows if row[0] not in gone]
    return table


PLANS_TEMP = "forever_plans_gone"


def plans_sql(recipe, learn):
    """The forever_item_template copy of the stock plans of a removed recipe, with each spell slot that holds
    a learn spell of the recipe emptied: the plans stay an item but teach nothing. It copies every patch row
    of the plans, because Forever::Table() hides all stock rows of the entry: a copy of the newest row only
    would take the item away on a realm of an earlier patch. Unless each stock plans row holds a learn spell
    in exactly one slot, the SQL fails instead of copying the rows. The copy keeps the patch of each stock
    row: the Timbermaw plans come with a later content patch, and must not appear earlier with the rules on."""
    teaching = ", ".join(str(spell) for spell in learn)
    slots = " + ".join("(`spellid_%d` IN (%s))" % (k, teaching) for k in range(1, MAX_ITEM_SPELLS + 1))
    lines = [
        "-- Removed recipe %s (%d): its plans %d teach nothing." % (recipe.name, recipe.spell, recipe.plans),
        "CREATE TEMPORARY TABLE `%s` SELECT * FROM `item_template` WHERE `entry`=%d;" % (PLANS_TEMP, recipe.plans),
        fail_unless("(SELECT COUNT(*) FROM `%s`)>0 AND (SELECT COUNT(*) FROM `%s` WHERE %s<>1)=0" % (
            PLANS_TEMP, PLANS_TEMP, slots), "forever_plans_teach"),
    ]
    for k in range(1, MAX_ITEM_SPELLS + 1):
        lines.append("UPDATE `%s` SET %s WHERE `spellid_%d` IN (%s);" % (
            PLANS_TEMP, ", ".join("`%s_%d`=%s" % (column, k, empty) for column, empty in SPELL_SLOT), k, teaching))
    return lines + [
        "INSERT INTO `%s` SELECT * FROM `%s`;" % (ITEM_TABLE, PLANS_TEMP),
        "DROP TEMPORARY TABLE `%s`;" % PLANS_TEMP,
    ]


def plans_server_sql(spec, stock):
    """The plans copies of the removed recipes that teach nothing (plans_sql). They are copies with SELECT *,
    so build.server_sql writes them before removals.marker_sql."""
    lines = []
    for recipe in sorted(spec.removed_recipes, key=lambda r: r.spell):
        if recipe.plans is not None:
            lines += [""] + plans_sql(recipe, stock_learn_spells(recipe.spell, stock))
    return lines


def server_sql(spec, stock):
    """The removal lines of the removed recipes: their stock skill line abilities and the trainer rows of
    their learn spells. build.server_sql writes them after removals.marker_sql."""
    lines = []
    for recipe in sorted(spec.removed_recipes, key=lambda r: r.spell):
        lines += [""]
        if stock_learn_spells(recipe.spell, stock):
            lines.append(no_trainer_template_sql(", ".join(str(s) for s in stock_learn_spells(recipe.spell, stock))))
        lines += removals.remove_rows("skill_line_ability", "id", [row[0] for row in stock_abilities(recipe, stock)],
                                      "Removed recipe %s (%d): its skill line ability." % (recipe.name, recipe.spell))
        lines += removals.remove_rows("npc_trainer", "spell", stock_learn_spells(recipe.spell, stock),
                                      "Removed recipe %s (%d): the trainer rows of its learn spells." % (recipe.name, recipe.spell))
    return lines
