#!/usr/bin/env python3
"""Writes the #230 rows of the spec: every new Forever Blacksmithing recipe (and the two new smelts) with its
made item, its plans and its new reagents, from the client dump of #224.

    uv run python tools/new_recipe_rows.py --dump <client-data>/out/professions [--stock-item-ids]

<dump> holds fv_new_recipes.tsv, items.tsv and stock/ (items.tsv, spells.tsv, sla.tsv: the stock world
database). The dumps stay outside git (only numbers and names go into the spec). The script replaces the #230
block of forever/spec/blacksmithing.tsv and of forever/spec/items.tsv, moves the "Next free" header lines on,
fills the plans of the #226 tracer recipe, and prints the explicit exclusions and the dead-content cut list
of the generator. Rows that the spec has already keep their IDs, so a refresh can run it again; a new row takes
the next free ID. With --stock-item-ids it also writes forever_gen/stock_item_ids.txt from stock/items.tsv.

Choices (see the block header in the spec files):
- A recipe copies the stock Blacksmithing recipe that makes its item's source item, else the stock Anvil
  recipe nearest in skill; a smelt copies Smelt Thorium (16153).
- A new item copies the stock item of the same class, subclass and slot nearest in item level (a stock
  Blacksmithing item first) and takes its display. Plans copy the stock Blacksmithing plans nearest in skill.
  New reagents copy the stock item named in REAGENT_SOURCES.
- Stats: as #227 (forever/REFRESH.md): ratings become 1.12 percents, the rest are bonuses of equip_stats.py.
- Out of scope, no rows: Season of Discovery rows, unnamed rows, camping objects and the rank and child-line
  rows. The plans in encrypted client data are listed and not filled from fan sites.
"""
import argparse
import csv
import os
import re
import sys

GENERATOR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, GENERATOR)

from forever_gen import dead_content, equip_stats, new_recipes, spec  # noqa: E402
from forever_gen.common import number_text  # noqa: E402

SPEC_DIR = os.path.join(GENERATOR, "..", "spec")
BLOCK_TICKET = "#230"
RECIPE_BLOCK = ("# --- #230 new recipes: every new Forever Blacksmithing recipe of build 1.60.1.70685 and the new\n"
                "# smelt Smelt Azerothium (written by forever/generator/tools/new_recipe_rows.py; do not edit by hand).")
ITEM_BLOCK = ("# --- #230 new items: the items, plans and new reagents of the #230 recipes (written by\n"
              "# forever/generator/tools/new_recipe_rows.py; do not edit by hand).")
TRACER_SPELL = 1252230  # #226 owns its recipe and item rows; #230 fills its plans.
SMELT_SOURCE = 16153  # Smelt Thorium
BLACKSMITHING, MINING = 164, 186
QUALITY = {"Poor": "poor", "Common": "common", "Uncommon": "uncommon", "Rare": "rare", "Epic": "epic"}
BONDING = {0: "none", 1: "pickup", 2: "equip", 3: "use"}
BASE = ("Str", "Agi", "Sta", "Int", "Spi", "Health", "Mana")
RES = {"HolyRes": "Holy", "FireRes": "Fire", "NatureRes": "Nature", "FrostRes": "Frost", "ShadowRes": "Shadow",
       "ArcaneRes": "Arcane"}
# Forever rating per 1.12 percent (level 60), as #227.
RATING = {"CritRating": ("Crit", 14), "HitRating": ("Hit", 10), "DodgeRating": ("Dodge", 12),
          "ParryRating": ("Parry", 15), "BlockRating": ("Block", 5), "DefRating": ("Defense", 1)}
DIRECT = {"AP": "AP", "SpellPower": "SpellPower", "Healing": "Healing"}
# The stock item that a new reagent copies (class, subclass, stack and model of a stock reagent of its kind).
REAGENT_SOURCES = {
    248815: 10620,  # Heavy Thorium Ore: Thorium Ore
    248820: 12363,  # Torbernite: Arcane Crystal
    248822: 13468,  # Death Lotus: Black Lotus
    249410: 3372,   # Sulfuric Acid: Leaded Vial
    249426: 10620,  # Pitchblende: Thorium Ore
    249427: 8170,   # Pristine Leather: Rugged Leather
    249432: 4359,   # Sandpaper: Handful of Copper Bolts
    249726: 12359,  # Azerothium Bar: Thorium Bar
    251290: 12359,  # Legionite Bar: Thorium Bar
    251291: 12359,  # Heavy Thorium Bar: Thorium Bar
    274030: 12803,  # Malleable Essence of Nature: Living Essence
}
# The explicit exclusions that are no recipe of the dump: the Heavy Thorium vein and its lock (no spec rows).
OTHER_EXCLUSIONS = [
    ("lock", 4170, "the Heavy Thorium Vein lock (Mining 300)", "Heavy Thorium: left out with the vein (no rows)"),
    ("object", 0, "Heavy Thorium Vein", "Heavy Thorium: no vein in the client data; not added (#232)"),
]
# item_template inventory_type of the weapon slots that the spec can name (items.SLOTS).
SLOT_NAMES = {13: "oneHand", 21: "mainHand", 22: "offHand", 17: "twoHand", 14: "shield", 15: "ranged", 23: "holdable"}
SKIP_NAMES = re.compile(r"^(Deprecated|Monster|Test|\[PH\])|\bDEP\b|\(old\)|OLD", re.I)


def read_tsv(path):
    with open(path, encoding="utf-8") as handle:
        return list(csv.DictReader(handle, delimiter="\t", quoting=csv.QUOTE_NONE))


class Dump:
    """The client dump and the stock world database dump."""

    def __init__(self, folder):
        self.recipes = read_tsv(os.path.join(folder, "fv_new_recipes.tsv"))
        # The Season of Discovery rows are only in recipes.tsv (the dump of every skill line row).
        known = {r["spell"] for r in self.recipes}
        self.recipes += [r for r in read_tsv(os.path.join(folder, "recipes.tsv"))
                         if r["group"] == "sod" and r["spell"] not in known]
        self.items = {int(r["item"]): r for r in read_tsv(os.path.join(folder, "items.tsv"))}
        self.stock = {}
        for r in read_tsv(os.path.join(folder, "stock", "items.tsv")):
            entry = int(r["entry"])
            if entry not in self.stock or int(r["patch"]) > int(self.stock[entry]["patch"]):
                self.stock[entry] = r
        sla = read_tsv(os.path.join(folder, "stock", "sla.tsv"))
        spells = {int(r["entry"]): r for r in read_tsv(os.path.join(folder, "stock", "spells.tsv"))}
        # The stock Blacksmithing recipes: (spell, made item, yellow, focus).
        self.stock_recipes = []
        for row in sla:
            spell = spells.get(int(row["spell_id"]))
            if int(row["skill_id"]) == BLACKSMITHING and spell and spell["effect1"] == "24":
                self.stock_recipes.append((int(row["spell_id"]), int(spell["effectItemType1"]),
                                           int(row["min_value"]), int(spell["requiresSpellFocus"])))
        self.stock_recipes.sort()
        self.made_by = {}
        for spell, made, _, _ in self.stock_recipes:
            self.made_by.setdefault(made, spell)
        self.stock_ids = sorted(self.stock)


def reagents_of(text):
    """[(Forever item, count)] of a dump reagents value 'Name xN (id), ...'."""
    return [(int(m.group(2)), int(m.group(1))) for m in re.finditer(r" x(\d+) \((\d+)\)", text)]


def stock_source(dump, cls, subclass, inventory_type, item_level):
    """The stock item of this class, subclass and slot nearest in item level, a Blacksmithing item first."""
    best = None
    for entry in dump.stock_ids:
        s = dump.stock[entry]
        if (int(s["class"]), int(s["subclass"]), int(s["inventory_type"])) != (cls, subclass, inventory_type):
            continue
        if SKIP_NAMES.search(s["name"]) or int(s["quality"]) > 4 or s["display_id"] == "0":
            continue
        key = (entry not in dump.made_by, abs(int(s["item_level"]) - item_level), entry)
        if best is None or key < best[0]:
            best = (key, entry)
    return best[1] if best else None


def plans_source(dump, skill):
    """The stock Blacksmithing plans nearest in skill."""
    best = None
    for entry in dump.stock_ids:
        s = dump.stock[entry]
        if (s["class"], s["subclass"], s["required_skill"]) != ("9", "4", "164") or SKIP_NAMES.search(s["name"]):
            continue
        key = (abs(int(s["required_skill_rank"]) - skill), entry)
        if best is None or key < best[0]:
            best = (key, entry)
    return best[1]


def recipe_source(dump, skill_line, source_item, yellow):
    if skill_line == MINING:
        return SMELT_SOURCE
    if source_item in dump.made_by:
        return dump.made_by[source_item]
    anvil = [(abs(y - yellow), spell) for spell, _, y, focus in dump.stock_recipes if focus == 1]
    return min(anvil)[1]


def stats_of(forever):
    """(base stats, bonuses text, resistances text, bonus armor, notes) of a Forever item row."""
    base, bonuses, res, notes, bonus_armor = [], [], [], [], 0
    parts = [p.strip().rsplit(" ", 1) for p in forever["stats"].split(",") if p.strip()]
    names = {n for n, _ in parts}
    for name, value in parts:
        v = int(value)
        if name in BASE:
            base.append("%s %d" % (name, v))
        elif name in RES:
            res.append("%s %d" % (RES[name], v))
        elif name == "BonusArmor":
            bonus_armor += v
        elif name in RATING or name in DIRECT or name == "SpellDmg":
            if name == "SpellDmg" and "Healing" in names:
                notes.append("SpellDmg %d is the damage part of the Forever healing stat: 1.12 healing gives none" % v)
                continue
            if name == "SpellDmg":
                bonus, amount = "SpellPower", v
                notes.append("Forever SpellDmg %d (damage only) is SpellPower" % v)
            elif name in RATING:
                bonus, per = RATING[name]
                amount = v / per
            else:
                bonus, amount = DIRECT[name], v
            if amount == int(amount) and int(amount) in equip_stats.BONUSES[bonus][1]:
                bonuses.append("%s %d" % (bonus, amount))
            else:
                bonuses.append("%s %d (Forever %s)" % (bonus, equip_stats.nearest(bonus, amount), number_text(amount)))
        else:
            raise SystemExit("item %s: the Forever stat %s has no 1.12 rule" % (forever["item"], name))
    return base, bonuses, res, bonus_armor, notes


def damage_of(forever):
    if not forever["damage"]:
        return None
    dmg, _, rest = forever["damage"].partition(" @")
    low, high = dmg.split("-")
    return int(low), int(high), int(round(float(rest.split(" ")[0]) * 1000))


class Ids:
    """The IDs that the spec gave out (kept on a rerun) and the next free ones."""

    def __init__(self, current):
        self.recipe = {r.forever_spell: r.spell for r in current.recipes if r.forever_spell}
        self.learn = {r.forever_spell: r.plans_spell for r in current.recipes if r.plans_spell}
        self.item = {i.forever_item: i.entry for i in current.items if i.kind == "item" and i.forever_item}
        used_spells = set(self.recipe.values()) | set(self.learn.values())
        self.next_spell = max(used_spells | {current.profession_id_range[0]}) + 1
        self.next_item = max(set(self.item.values()) | {current.item_id_range[0]}) + 1

    def spell_of(self, table, forever):
        if forever not in table:
            table[forever] = self.next_spell
            self.next_spell += 1
        return table[forever]

    def item_of(self, forever):
        if forever not in self.item:
            self.item[forever] = self.next_item
            self.next_item += 1
        return self.item[forever]


def build(dump, current):
    """(recipe rows, item rows, tracer plans value, exclusions [(what, id, name, why)], the Ids used)."""
    ids = Ids(current)
    exclusions = list(OTHER_EXCLUSIONS)
    chosen = []
    for r in sorted(dump.recipes, key=lambda r: int(r["spell"])):
        spell = int(r["spell"])
        if r["group"] != "fv-new":
            why = {"sod": "Season of Discovery row", "fv-camping": "camping object (needs C++)",
                   "fv-other": "rank, child-line or unnamed row"}.get(r["group"], r["group"])
            exclusions.append(("recipe", spell, r["name"] or "(unnamed)", why))
            continue
        chosen.append(r)
    for r in chosen:  # recipe IDs first, in Forever order
        ids.spell_of(ids.recipe, int(r["spell"]))
    recipe_rows, item_rows, made_rows, plans_rows, reagent_rows = [], [], [], [], []
    tracer_plans = None
    seen_reagents = set()

    def item_id(forever):
        if forever in dump.stock:
            return forever
        return ids.item_of(forever)

    for r in chosen:
        spell = int(r["spell"])
        made = int(r["creates_id"])
        skill_line = int(r["skill"])
        notes = ["From Forever spell %d (%s)." % (spell, r["category"])]
        source_item = made if made in dump.stock else None
        if made in REAGENT_SOURCES:
            made_rows.append(reagent_row(dump, made, item_id(made), made_by=r["name"]))
        elif made not in dump.stock and spell != TRACER_SPELL:
            row, source_item = item_row(dump, made, item_id(made))
            made_rows.append(row)
        elif spell == TRACER_SPELL:
            source_item = 3469
        if r["itemset"]:
            notes.append("Forever item set %s (#231)." % r["itemset"])
        plans, min_rank = "-", 1
        if r["plans"]:
            forever_plans = int(r["plans"])
            p = dump.items.get(forever_plans)
            if p and p["status"] == "new-id":
                min_rank = int(r["plans_reqskill"].split("/")[1])
                learn = ids.spell_of(ids.learn, spell)
                plans = "%d/%d" % (item_id(forever_plans), learn)
                plans_rows.append(plans_row(dump, p, item_id(forever_plans), min_rank))
            else:
                exclusions.append(("item", forever_plans, "plans of %s" % r["name"],
                                   "encrypted in the client data (build 1.60.1.70685); not filled from fan sites"))
                notes.append("Plans %d are in encrypted client data: excluded." % forever_plans)
        else:
            notes.append("No plans in the client (learned with a GM command; trainer: a later ticket).")
        if spell == TRACER_SPELL:
            tracer_plans = (plans, min_rank)
            continue
        reagents = []
        for forever, count in reagents_of(r["reagents"]):
            reagents.append("%dx%d" % (item_id(forever), count))
            if forever not in dump.stock and forever not in seen_reagents and forever not in dump_made(chosen):
                seen_reagents.add(forever)
                reagent_rows.append(reagent_row(dump, forever, item_id(forever)))
        if r["focus"] in dead_content.FOREVER_STATIONS:
            notes.append("Needs a %s: the dead-content rule cuts it." % r["focus"])
        source = recipe_source(dump, skill_line, source_item, int(r["yellow"]))
        recipe_rows.append([
            "recipe", str(ids.recipe[spell]), str(source), r["name"], str(skill_line), str(min_rank), r["yellow"],
            r["grey"], ",".join(reagents), "%dx1" % item_id(made), r["focus"], plans, str(spell), " ".join(notes)])
    item_rows = made_rows + reagent_rows + plans_rows
    item_rows.sort(key=lambda row: int(row[1]))
    return recipe_rows, item_rows, tracer_plans, exclusions, ids


_MADE = {}


def dump_made(chosen):
    """The Forever items that the chosen recipes make (their rows are made-item rows, not reagent rows)."""
    key = id(chosen)
    if key not in _MADE:
        _MADE[key] = {int(r["creates_id"]) for r in chosen}
    return _MADE[key]


def item_row(dump, forever_id, entry):
    """(the items.tsv row of a made item, its stock source item)."""
    f = dump.items[forever_id]
    if f["status"] != "new-id":
        raise SystemExit("item %d: %s" % (forever_id, f["status"]))
    cls, subclass = (int(v) for v in f["cls"].split("/"))
    inventory_type = int(f["inv"])
    source = stock_source(dump, cls, subclass, inventory_type, int(f["ilvl"]))
    if source is None:
        raise SystemExit("item %d %s: no stock item of class %d/%d and slot %d" % (forever_id, f["name"], cls, subclass,
                                                                                 inventory_type))
    s = dump.stock[source]
    base, bonuses, res, bonus_armor, notes = stats_of(f)
    damage = damage_of(f)
    armor = (int(f["armor"]) if f["armor"] else 0) + bonus_armor
    for effect in [e.strip() for e in f["effects"].split(";") if e.strip()]:
        notes.append("the Forever effect '%s' is no stock spell: not ported" % effect)
    notes.insert(0, "Copy of %s (%d): its model and icon." % (s["name"], source))
    row = ["item", str(entry), str(source), f["name"], QUALITY[f["quality"]], f["ilvl"], f["reqlvl"],
           BONDING[int(f["bonding"])], "-",
           str(damage[2]) if damage else "-", "%d-%d" % damage[:2] if damage else "-", str(armor),
           ", ".join(res) or "none", ", ".join(base) or "-", ", ".join(bonuses) or "-", "-", s["display_id"],
           f["sell"] or "0", str(forever_id), f["stats"] or "-", "; ".join(n.rstrip(".") for n in notes) + "."]
    return row, source


def plans_row(dump, f, entry, skill):
    source = plans_source(dump, skill)
    s = dump.stock[source]
    return ["item", str(entry), str(source), f["name"], QUALITY[f["quality"]], f["ilvl"], f["reqlvl"] or "0",
            BONDING[int(f["bonding"] or 0)], "-", "-", "-", "0", "none", "-", "-", "-", s["display_id"], f["sell"] or "0",
            f["item"], "-", "Plans, copy of %s (%d). Needs Blacksmithing %d. No source yet (a later ticket)."
            % (s["name"], source, skill)]


def reagent_row(dump, forever_id, entry, made_by=None):
    """A new reagent: a copy of its stock source in REAGENT_SOURCES. `made_by`: the recipe that makes it."""
    f = dump.items[forever_id]
    source = REAGENT_SOURCES.get(forever_id)
    if source is None:
        raise SystemExit("reagent %d %s: no stock source in REAGENT_SOURCES" % (forever_id, f["name"]))
    s = dump.stock[source]
    origin = ("Made by %s." % made_by if made_by
              else "Its source (vein, drop or vendor) is a later ticket.")
    return ["item", str(entry), str(source), f["name"], QUALITY[f["quality"]], f["ilvl"], f["reqlvl"] or "0",
            BONDING[int(f["bonding"] or 0)], "-", "-", "-", "0", "none", "-", "-", "-", s["display_id"], f["sell"] or "0",
            str(forever_id), "-", "Reagent, copy of %s (%d). %s" % (s["name"], source, origin)]


def wrap(text, width=110):
    """Comment lines of at most `width` characters."""
    lines, line = [], "#"
    for word in text.split(" "):
        if len(line) + 1 + len(word) > width:
            lines.append(line)
            line = "#  "
        line += " " + word
    return lines + [line]


def block_comments(exclusions, kind):
    """The comment lines under the block line: the source, the rule and the exclusions."""
    encrypted = ", ".join(str(key) for what, key, _, why in exclusions if what == "item" and why.startswith("encrypted"))
    counts = {}
    for what, _, _, why in exclusions:
        if what == "recipe":
            counts[why] = counts.get(why, 0) + 1
    text = ["Source: fv_new_recipes.tsv and items.tsv of #224 (WoW Forever build 1.60.1.70685), over the stock world "
            "DB (item_template, spell_template, skill_line_ability)."]
    if kind == "recipe":
        text += ["The rows keep every readable new recipe, also those that cannot work on a 1.12 realm: the "
                 "generator's dead-content rule (forever_gen/dead_content.py) cuts the Master Forge and Molten "
                 "Foundry recipes and all that is then left with no use (Heavy Thorium and what needs it, the "
                 "Glory and Justice sets, Goregasher and Clutchlord, the Ironwood set), and prints the cut list.",
                 "minRank: the skill that the plans need (RequiredSkillRank in the client), else 1. plans: "
                 "'<plans item>/<learn spell>'. A recipe copies the stock recipe of its item's source item, else "
                 "the stock Anvil recipe nearest in skill; a smelt copies Smelt Thorium.",
                 "Excluded, no rows: %s." % "; ".join("%d %s" % (n, why) for why, n in sorted(counts.items()))]
    else:
        text += ["A made item copies the stock item of its class, subclass and slot nearest in item level (a "
                 "Blacksmithing item first) and takes its model; plans copy the stock plans nearest in skill; a "
                 "new reagent copies a stock reagent of its kind. No new item has a source yet (later tickets). "
                 "Forever on-hit effects of the thrown hammers have no stock spell and are not ported."]
    text += ["Excluded (encrypted in the client data, not filled from fan sites): the plans %s." % encrypted]
    lines = []
    for paragraph in text:
        lines += wrap(paragraph)
    return lines


def replace_block(text, block, rows, column_line, comments=()):
    """The file text with the #230 block (its comment lines and rows) replaced, at the end of the file."""
    lines = text.rstrip("\n").split("\n")
    out, inside = [], False
    for line in lines:
        if line.startswith("# --- "):
            inside = BLOCK_TICKET in line.split(" ")[2:3]
        if not inside:
            out.append(line)
    while out and out[-1] == "":
        out.pop()
    out += ["", block] + list(comments) + ["\t".join(row) for row in rows]
    assert column_line in out
    return "\n".join(out) + "\n"


def set_next_free(text, label, value):
    return re.sub(r"(Next free %s: )\d+" % label, lambda m: m.group(1) + str(value), text)


def set_tracer_plans(text, plans, min_rank):
    """Fills the plans of the #226 tracer recipe and the skill that they need."""
    out = []
    for line in text.split("\n"):
        values = line.split("\t")
        if len(values) == 14 and values[0] == "recipe" and values[12] == str(TRACER_SPELL):
            values[5], values[11] = str(min_rank), plans
            line = "\t".join(values)
        out.append(line)
    return "\n".join(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--dump", required=True, help="the out/professions folder of the client dump")
    parser.add_argument("--stock-item-ids", action="store_true", help="also write forever_gen/stock_item_ids.txt")
    args = parser.parse_args()
    dump = Dump(args.dump)
    if args.stock_item_ids:
        with open(new_recipes.STOCK_ITEM_IDS, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(new_recipes.ranges_text(dump.stock_ids))
    current = spec.read(SPEC_DIR)
    recipe_rows, item_rows, tracer_plans, exclusions, ids = build(dump, current)
    files = {name: open(os.path.join(SPEC_DIR, name), encoding="utf-8").read() for name in ("blacksmithing.tsv", "items.tsv")}
    from forever_gen import items, recipes
    text = replace_block(files["blacksmithing.tsv"], RECIPE_BLOCK, recipe_rows, "\t".join(recipes.COLUMNS),
                         block_comments(exclusions, "recipe"))
    text = set_next_free(set_tracer_plans(text, *tracer_plans), "profession spell ID", ids.next_spell)
    files["blacksmithing.tsv"] = text
    text = replace_block(files["items.tsv"], ITEM_BLOCK, item_rows, "\t".join(items.COLUMNS),
                         block_comments(exclusions, "item"))
    files["items.tsv"] = set_next_free(text, "item ID", ids.next_item)
    for name, text in files.items():
        with open(os.path.join(SPEC_DIR, name), "w", encoding="utf-8", newline="\n") as handle:
            handle.write(text)
    _, cuts = dead_content.cut(spec.read(SPEC_DIR))
    print("Wrote %d recipe rows and %d item rows (#230)." % (len(recipe_rows), len(item_rows)))
    print("Explicit exclusions (%d rows, no spec rows):" % len(exclusions))
    for what, key, name, why in exclusions:
        print("  %s\t%d\t%s\t%s" % (what, key, name, why))
    print(dead_content.cut_list_text(cuts), end="")


if __name__ == "__main__":
    main()
