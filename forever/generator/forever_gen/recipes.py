"""The recipe rows of the profession spec files (blacksmithing.tsv, #226): one new recipe spell per row.

A recipe spell is a copy of a stock recipe spell (its cast time, tool, icon and visuals stay) with its own
name, reagents, made item and spell focus. It gets a skill line ability with the skill line, the skill that
learning it needs and the colour range. build.py writes both into the client data patch and the server SQL.
"""
from collections import namedtuple

from .spec import SpecError, rows_of

# reagents: [(item, count)]; creates: (item, count); focus: the name of a stock SpellFocusObject.dbc row, or
# NO_VALUE; plans: the stock plans item of a stock row, or None.
Recipe = namedtuple("Recipe", "kind spell source_spell name skill_line min_rank yellow grey reagents creates focus "
                              "plans forever_spell notes")

COLUMNS = ("kind", "spell", "sourceSpell", "name", "skillLine", "minRank", "yellow", "grey", "reagents", "creates",
           "focus", "plans", "foreverSpell", "notes")
# recipe: a new recipe spell (here). stock: a stock recipe that Forever changes in place (stock_recipes.py, #228).
KINDS = ("recipe", "stock")
NO_VALUE = "-"
MAX_REAGENTS = 8
# The spell_template columns of the reagents (Spell.dbc fields 42-49 and 50-57).
REAGENT_COLUMNS = ["reagent%d" % k for k in range(1, MAX_REAGENTS + 1)]
REAGENT_COUNT_COLUMNS = ["reagentCount%d" % k for k in range(1, MAX_REAGENTS + 1)]
# SpellFocusObject.dbc 1.12.1: 0 ID, 1 Name (enUS).
FOCUS_NAME = 1


def _counted(where, column, text):
    try:
        pairs = [tuple(int(v) for v in part.split("x")) for part in text.split(",")]
    except ValueError:
        pairs = None
    if not pairs or any(len(pair) != 2 or pair[1] < 1 for pair in pairs):
        raise SpecError("%s: %s '%s' is not 'item x count, ...'" % (where, column, text))
    return pairs


def read_rows(path, lines):
    """{"recipes": the new recipes, "stock_recipes": the stock rows}."""
    recipes = []
    for where, raw in rows_of(path, lines, COLUMNS):
        if raw["kind"] not in KINDS:
            raise SpecError("%s: unknown kind '%s'" % (where, raw["kind"]))
        reagents = _counted(where, "reagents", raw["reagents"])
        if len(reagents) > MAX_REAGENTS:
            raise SpecError("%s: %d reagents, at most %d" % (where, len(reagents), MAX_REAGENTS))
        creates = _counted(where, "creates", raw["creates"])
        if len(creates) != 1:
            raise SpecError("%s: a recipe makes one item" % where)
        recipes.append(Recipe(
            kind=raw["kind"], spell=int(raw["spell"]), source_spell=int(raw["sourceSpell"]), name=raw["name"],
            skill_line=int(raw["skillLine"]), min_rank=int(raw["minRank"]), yellow=int(raw["yellow"]),
            grey=int(raw["grey"]), reagents=reagents, creates=creates[0], focus=raw["focus"],
            plans=None if raw["plans"] == NO_VALUE else int(raw["plans"]),
            forever_spell=int(raw["foreverSpell"]), notes=raw["notes"]))
    return {"recipes": [r for r in recipes if r.kind == "recipe"],
            "stock_recipes": [r for r in recipes if r.kind == "stock"]}


def focus_ids(stock):
    """{name: ID} of the stock SpellFocusObject.dbc."""
    return {stock.spell_focus.text(row[FOCUS_NAME]): row[0] for row in stock.spell_focus.rows}


def check(spec, stock, used_spells):
    """The ID rules of the recipes: a new spell in the profession spell range, no clash with a stock spell or
    another spec spell (`used_spells`: the spell IDs of the other rows, none of them in the profession
    range), a stock source spell and a stock spell focus."""
    low, high = spec.profession_id_range or (0, -1)
    if spec.recipes and not spec.id_range[0] <= low <= high <= spec.id_range[1]:
        raise SpecError("the free profession spell ID range %d-%d is not inside the free spell ID range %d-%d"
                        % ((low, high) + spec.id_range))
    for spell in sorted(used_spells):
        if low <= spell <= high:
            # The core tells a profession spell from a Warrior spell by this block, even with the rules off.
            raise SpecError("spell row spell %d is inside the profession spell ID range %d-%d" % (spell, low, high))
    stock_spells = stock.spell.by_id()
    foci = focus_ids(stock)
    seen = set(used_spells)
    for recipe in spec.recipes:
        where = "recipe %s: spell %d" % (recipe.name, recipe.spell)
        if recipe.spell in seen or recipe.spell in stock_spells:
            raise SpecError("%s clashes with another spell of the same ID" % where)
        seen.add(recipe.spell)
        if not low <= recipe.spell <= high:
            raise SpecError("%s is outside the free profession spell ID range %d-%d" % (where, low, high))
        if recipe.source_spell not in stock_spells:
            raise SpecError("%s: the source spell %d is no stock spell" % (where, recipe.source_spell))
        if recipe.focus not in foci:
            raise SpecError("%s: the focus '%s' is no stock SpellFocusObject.dbc name" % (where, recipe.focus))


def spell_changes(recipe, stock):
    """The spell_template columns that a recipe sets on the copy of its source spell, in this order: the spell
    focus, the reagents and their counts, and the made item with its count (base points + die sides 1)."""
    padding = [0] * (MAX_REAGENTS - len(recipe.reagents))
    item, count = recipe.creates
    return ([("requiresSpellFocus", focus_ids(stock)[recipe.focus])]
            + list(zip(REAGENT_COLUMNS, [r for r, _ in recipe.reagents] + padding))
            + list(zip(REAGENT_COUNT_COLUMNS, [c for _, c in recipe.reagents] + padding))
            + [("effectItemType1", item), ("effectBasePoints1", count - 1), ("effectDieSides1", 1),
               ("effectBaseDice1", 1)])
