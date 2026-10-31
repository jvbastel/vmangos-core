"""The trainer file of the new recipes (blacksmithing_trainers.tsv, #239): how a character learns a new recipe
that the Forever trainers teach.

A trainer row (kind `trainer`) gives a new recipe the trainers of one stock recipe (`trainersFrom`): the SQL
copies every stock npc_trainer row of the stock recipe's learn spell into forever_npc_trainer, with the new
recipe's learn spell, skill (its minRank) and cost. The core reads forever_npc_trainer over npc_trainer by
`spell` (ObjectMgr::LoadTrainers), so the rows of a new learn spell join the stock lists. The learn spell is the
one of the recipe's plans (new_recipes.py): a copy of 2760, whose first effect is Learn Spell, as the core needs
of a trainer spell; its SpellVisual 222 makes the player cast it (HandleTrainerBuySpellOpcode), as on the five
stock trainer spells of that kind. The plans stay as they are.

An auto row (kind `auto`) gives a new recipe with the profession skill, with no trainer step: its skill line
ability gets AcquireMethod 1 (client SkillLineAbility.dbc field 9, server learn_on_get_skill), as stock Rough
Copper Vest 12260 has. The core teaches it in Player::UpdateSkillTrainedSpells.
"""
from collections import namedtuple

from .common import NO_VALUE, SpecError, check_kind, fail_unless, rows_of
from .stock_recipes import TRAINER_TABLE, no_trainer_template_sql, stock_learn_spells

COLUMNS = ("kind", "recipe", "name", "learnSpell", "trainersFrom", "cost", "notes")
TRAINER, AUTO = "trainer", "auto"
KINDS = (TRAINER, AUTO)
# The columns that an auto row leaves empty (NO_VALUE).
TRAINER_ONLY = ("learnSpell", "trainersFrom", "cost")
# SkillLineAbility AcquireMethod 1: learned with the profession skill (ABILITY_LEARNED_ON_GET_PROFESSION_SKILL).
LEARNED_WITH_SKILL = 1

# learn_spell, trainers_from (the stock recipe spell) and cost (copper): None on an auto row.
Trainer = namedtuple("Trainer", "kind recipe name learn_spell trainers_from cost notes")


def read_rows(path, lines):
    """{"trainers": the rows of the trainer file}."""
    trainers = []
    for where, raw in rows_of(path, lines, COLUMNS):
        check_kind(where, raw["kind"], KINDS)
        values = {column: raw[column] for column in TRAINER_ONLY}
        if raw["kind"] == AUTO:
            if any(value != NO_VALUE for value in values.values()):
                raise SpecError("%s: an auto row has no %s (write %s)" % (where, ", ".join(TRAINER_ONLY), NO_VALUE))
            numbers = {column: None for column in TRAINER_ONLY}
        else:
            try:
                numbers = {column: int(value) for column, value in values.items()}
            except ValueError:
                raise SpecError("%s: a trainer row needs a number in %s" % (where, ", ".join(TRAINER_ONLY)))
        trainers.append(Trainer(kind=raw["kind"], recipe=int(raw["recipe"]), name=raw["name"],
                                learn_spell=numbers["learnSpell"], trainers_from=numbers["trainersFrom"],
                                cost=numbers["cost"], notes=raw["notes"]))
    return {"trainers": trainers}


def check(spec, stock):
    """On the spec after the dead-content rule: each row names a live new recipe once, by its name. A trainer
    row casts the learn spell of the recipe's plans and copies the trainers of a stock recipe with one stock learn
    spell, which no removal row takes away."""
    recipes = {recipe.spell: recipe for recipe in spec.recipes}
    removed = {recipe.spell for recipe in spec.removed_recipes}
    seen = set()
    for row in spec.trainers:
        where = "trainer row %s: recipe %d" % (row.name, row.recipe)
        recipe = recipes.get(row.recipe)
        if recipe is None:
            raise SpecError("%s is no new recipe of the spec, or the dead-content rule cuts it" % where)
        if row.recipe in seen:
            raise SpecError("%s is in the trainer file twice" % where)
        seen.add(row.recipe)
        if row.name != recipe.name:
            raise SpecError("%s: the recipe's name is '%s'" % (where, recipe.name))
        if row.kind == AUTO:
            continue
        if row.learn_spell != recipe.plans_spell:
            raise SpecError("%s: the learn spell %d is not the learn spell of the recipe's plans (%s)"
                            % (where, row.learn_spell, recipe.plans_spell))
        if row.trainers_from in removed:
            raise SpecError("%s: a removal row takes the stock recipe %d away from the trainers" % (where, row.trainers_from))
        if len(stock_learn_spells(row.trainers_from, stock)) != 1:
            raise SpecError("%s: the stock recipe %d has not one stock learn spell" % (where, row.trainers_from))
        if row.cost < 0:
            raise SpecError("%s: the cost %d is below 0" % (where, row.cost))


def auto_learned(spec):
    """The recipe spells that a character learns with the profession skill."""
    return {row.recipe for row in spec.trainers if row.kind == AUTO}


def server_sql(spec, stock):
    """The forever_npc_trainer rows of each trainer row: a copy of the stock trainer rows of its stock recipe's
    learn spell, with the new learn spell, skill and cost. The SQL fails when a trainer template teaches the stock
    learn spell (the copy would miss those trainers), when no trainer teaches it, or when a trainer teaches it
    below the recipe's skill (a trainer whose rank caps below the skill would teach the recipe)."""
    recipes = {recipe.spell: recipe for recipe in spec.recipes}
    lines = []
    for row in sorted((r for r in spec.trainers if r.kind == TRAINER), key=lambda r: r.recipe):
        skill = recipes[row.recipe].min_rank
        source = stock_learn_spells(row.trainers_from, stock)[0]
        lines += [
            "",
            "-- Recipe %s (%d): the trainers of stock recipe %d teach it with spell %d at skill %d for %d copper."
            % (row.name, row.recipe, row.trainers_from, row.learn_spell, skill, row.cost),
            no_trainer_template_sql(str(source)),
            "CREATE TEMPORARY TABLE `forever_trainer_new` SELECT * FROM `npc_trainer` WHERE `spell`=%d;" % source,
            fail_unless("(SELECT COUNT(*) > 0 AND MIN(`reqskillvalue`) >= %d FROM `forever_trainer_new`)" % skill,
                        "forever_found"),
            "UPDATE `forever_trainer_new` SET `spell`=%d, `reqskillvalue`=%d, `spellcost`=%d;"
            % (row.learn_spell, skill, row.cost),
            "INSERT INTO `%s` SELECT * FROM `forever_trainer_new`;" % TRAINER_TABLE,
            "DROP TEMPORARY TABLE `forever_trainer_new`;",
        ]
    return lines
