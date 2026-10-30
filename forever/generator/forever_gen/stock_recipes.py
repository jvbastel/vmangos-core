"""The stock rows of the recipe files (blacksmithing.tsv, #228): stock recipes that Forever changes in place.

A stock row keeps the stock spell ID, so that a character who knows the recipe keeps it. It changes:
- the colour range of the stock skill line ability (client SkillLineAbility.dbc in place, and a
  forever_skill_line_ability copy of the stock row by its ID),
- the skill that its trainer rows need (forever_npc_trainer: a copy of every stock npc_trainer row of the
  recipe's learn spells, with the new reqskillvalue; the core reads it over npc_trainer by `spell`),
- the skill that its plans need (forever_item_template: a copy of the stock plans row, with the new
  required_skill_rank),
- its reagents, name and spell focus, where they differ from the stock spell (client Spell.dbc and
  forever_spell_template).
The generator has no world database: the copies are SQL that runs on the stock rows at load time.
"""
from .spec import SpecError

TRAINER_TABLE = "forever_npc_trainer"
ITEM_TABLE = "forever_item_template"
NO_FOCUS = "-"
# The world database build of the skill line ability rows (SUPPORTED_CLIENT_BUILD of the core).
CLIENT_BUILD = 5875
# Spell.dbc 1.12.1: 15 RequiresSpellFocus, 42-49 Reagent, 50-57 ReagentCount, 61 Effect1, 64 EffectDieSides1,
# 76 EffectBasePoints1, 103 EffectItemType1, 109 EffectTriggerSpell1, 120 Name (enUS), 129 Rank (enUS).
SPELL_FOCUS, SPELL_REAGENTS, SPELL_REAGENT_COUNTS = 15, 42, 50
SPELL_EFFECT1, SPELL_DIE_SIDES1, SPELL_BASE_POINTS1, SPELL_ITEM_TYPE1, SPELL_TRIGGER1 = 61, 64, 76, 103, 109
SPELL_NAME, SPELL_RANK = 120, 129
SPELL_EFFECT_LEARN_SPELL = 36
MAX_REAGENTS = 8
# SkillLineAbility.dbc 1.12.1: 0 ID, 1 SkillLine, 2 Spell, 10 grey, 11 yellow.
SLA_SKILL, SLA_SPELL, SLA_GREY, SLA_YELLOW = 1, 2, 10, 11


def _int32(value):
    return value - (1 << 32) if value >= 1 << 31 else value


def stock_abilities(recipe, stock):
    """The stock SkillLineAbility.dbc rows of a stock row: its spell in its skill line (one row, or two for
    Radiant Circlet 16659)."""
    return [row for row in stock.skill_line_ability.rows
            if row[SLA_SPELL] == recipe.spell and row[SLA_SKILL] == recipe.skill_line]


def focus_id(recipe, stock):
    if recipe.focus == NO_FOCUS:
        return 0
    from .recipes import focus_ids
    return focus_ids(stock).get(recipe.focus)


def stock_reagents(row):
    return [(row[SPELL_REAGENTS + k], row[SPELL_REAGENT_COUNTS + k]) for k in range(MAX_REAGENTS)
            if row[SPELL_REAGENTS + k]]


def check(spec, stock):
    """A stock row names a stock recipe spell (spell = sourceSpell) with a stock skill line ability in its
    skill line, keeps the item that the stock spell makes, has a stock focus or none, and its plans item is
    not also an item row of the spec. No stock spell twice."""
    stock_spells = stock.spell.by_id()
    item_rows = {item.entry for item in spec.items}
    seen = set()
    for recipe in spec.stock_recipes:
        where = "stock recipe %s: spell %d" % (recipe.name, recipe.spell)
        if recipe.spell in seen:
            raise SpecError("%s is in the spec twice" % where)
        seen.add(recipe.spell)
        if recipe.spell not in stock_spells:
            raise SpecError("%s is no stock spell" % where)
        if recipe.source_spell != recipe.spell:
            raise SpecError("%s: a stock row changes the stock spell in place (sourceSpell %d)" % (where, recipe.source_spell))
        if not stock_abilities(recipe, stock):
            raise SpecError("%s has no stock skill line ability in skill line %d" % (where, recipe.skill_line))
        row = stock_spells[recipe.spell]
        made = (row[SPELL_ITEM_TYPE1], _int32(row[SPELL_BASE_POINTS1]) + row[SPELL_DIE_SIDES1])
        if recipe.creates != made:
            raise SpecError("%s: a stock row keeps the item that the stock spell makes (%dx%d)" % ((where,) + made))
        if focus_id(recipe, stock) is None:
            raise SpecError("%s: the focus '%s' is no stock SpellFocusObject.dbc name" % (where, recipe.focus))
        if recipe.plans is not None and recipe.plans in item_rows:
            raise SpecError("%s: its plans %d are also an item row of the spec" % (where, recipe.plans))


def apply_colours(table, spec, stock):
    """Sets the colour range of each stock row on its stock row of the client SkillLineAbility.dbc `table`
    (a copy of the stock table). The other fields stay."""
    by_id = table.by_id()
    for recipe in spec.stock_recipes:
        for ability in stock_abilities(recipe, stock):
            row = by_id[ability[0]]
            row[SLA_GREY], row[SLA_YELLOW] = recipe.grey, recipe.yellow
    return table


def learn_spells(recipe, stock):
    """The stock spells that teach the recipe (Effect1 Learn Spell on it): the spells of its trainer rows.
    The core takes a trainer row only for a spell whose first effect is Learn Spell."""
    return sorted(row[0] for row in stock.spell.rows
                  if row[SPELL_EFFECT1] == SPELL_EFFECT_LEARN_SPELL and row[SPELL_TRIGGER1] == recipe.spell)


def server_sql(spec, stock):
    """The server rows of the stock rows: the skill line abilities (forever_skill_line_ability, by the stock
    ID), the trainer rows (forever_npc_trainer) and the plans (forever_item_template)."""
    lines = []
    for recipe in sorted(spec.stock_recipes, key=lambda r: r.spell):
        lines += ["", "-- Stock recipe %s (%d): colour range %d-%d, learned at skill %d."
                  % (recipe.name, recipe.spell, recipe.yellow, recipe.grey, recipe.min_rank)]
        for ability in stock_abilities(recipe, stock):
            lines += [
                "CREATE TEMPORARY TABLE `forever_sla_stock` SELECT * FROM `skill_line_ability` WHERE `id`=%d "
                "AND `build`=%d;" % (ability[0], CLIENT_BUILD),
                "UPDATE `forever_sla_stock` SET `id`=%d, `min_value`=%d, `max_value`=%d;"
                % (ability[0], recipe.yellow, recipe.grey),
                "INSERT INTO `forever_skill_line_ability` SELECT * FROM `forever_sla_stock`;",
                "DROP TEMPORARY TABLE `forever_sla_stock`;",
            ]
        teaching = ", ".join(str(spell) for spell in learn_spells(recipe, stock))
        if teaching:
            # Every stock trainer row of the learn spells: the core reads forever_npc_trainer over npc_trainer
            # by `spell`, so the copy must hold all of them.
            lines += [
                "INSERT INTO `%s` SELECT * FROM `npc_trainer` WHERE `spell` IN (%s);" % (TRAINER_TABLE, teaching),
                "UPDATE `%s` SET `reqskillvalue`=%d WHERE `spell` IN (%s);" % (TRAINER_TABLE, recipe.min_rank, teaching),
            ]
        if recipe.plans is not None:
            lines += [
                "CREATE TEMPORARY TABLE `forever_plans_new` SELECT * FROM `item_template` WHERE `entry`=%d "
                "ORDER BY `patch` DESC LIMIT 1;" % recipe.plans,
                "UPDATE `forever_plans_new` SET `entry`=%d, `patch`=0, `required_skill_rank`=%d;"
                % (recipe.plans, recipe.min_rank),
                "INSERT INTO `%s` SELECT * FROM `forever_plans_new`;" % ITEM_TABLE,
                "DROP TEMPORARY TABLE `forever_plans_new`;",
            ]
    return lines
