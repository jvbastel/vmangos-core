"""The logic of the template builder page: hand-made gear templates.

builder_web.py serves the page. This module holds what the page does with
the item data of gear_data.py: the search per slot, the paste box, the
checks of a hand-made template, its empty slots, and the reading and
writing of its file. See step 4 of the version 1 build spec, "Template
builder page", and src/bots/GearTemplate.h for the file format.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path

import gear_data
import generate



# A Wowhead link has item=<entry>. Other numbers count only as whole words,
# not as a part of a link or a name.
_PASTE_ENTRY = re.compile(r"item=(\d+)|(?<![\w/=.?&-])(\d+)(?![\w/.])")


def read_paste(text):
    """The item entries in the text: plain numbers and Wowhead item links
    (`.../item=6448/tail-spike`), in their order."""
    return [int(link or number) for link, number in _PASTE_ENTRY.findall(text)]


@dataclass(frozen=True)
class PastedEntry:
    entry: int
    item: object = None   # The gear_data.Item, or None.
    problem: str = ""

    @property
    def ok(self):
        return not self.problem


def check_paste(entries, world):
    """Checks that each entry is a gear item of the 1.12 item data."""
    result = []
    for entry in entries:
        if entry in world.items and world.items[entry].slot:
            result.append(PastedEntry(entry, world.items[entry]))
        elif entry in world.entries:
            result.append(PastedEntry(entry, problem=f"item {entry} is not a gear item"))
        else:
            result.append(PastedEntry(entry, problem=f"no item {entry} in the 1.12 item data"))
    return result


def fits_slot(item, slot):
    """True when the item can fill the slot of the format."""
    return item.slot == slot or (item.slot == "onehand" and slot in ("mainhand", "offhand"))


def first_slot(item):
    """The slot of the format for the item: the main hand for a one-handed
    weapon."""
    return "mainhand" if item.slot == "onehand" else item.slot


def class_can_wear(item, slot, class_name):
    """True when a character of the class can use the item in the slot, at
    some level: the class mask of the item, the armor types and the weapon
    types of the class."""
    if not item.usable_by(gear_data.CLASS_IDS[class_name]):
        return False
    armor_types, weapon_types = generate.CLASSES[class_name]
    if slot in generate.ARMOR_SLOTS or slot == "shield":
        return item.armor_type in armor_types
    if item.weapon_type:
        return item.weapon_type in weapon_types
    return True


@dataclass(frozen=True)
class Filters:
    """The filters of the search. Empty sets and None take everything."""
    class_name: str | None = None   # Only the items that the class can use.
    level_min: int | None = None    # Required level from level_min to level_max.
    level_max: int | None = None
    qualities: frozenset = frozenset()
    source_kinds: frozenset = frozenset()   # Kinds of gear_data.Source, and "none" for no source.
    patch: int | None = None        # Only items and sources of this content patch.
    armor_types: frozenset = frozenset()
    text: str = ""                  # A part of the item name.


@dataclass(frozen=True)
class Found:
    item: object         # gear_data.Item
    score: float         # The stat weights score of the role, 0 with no weights.
    sources: list        # The gear_data.Source of the item, in the patch of the filter.
    banned: bool         # In the ban list of the generator.


def sources_in(world, entry, patch):
    sources = world.sources.get(entry, [])
    if patch is None:
        return [source for source in sources if any(level is not None for level in source.levels)]
    return [source for source in sources if source.levels[patch] is not None]


def search(world, slot, filters, weights, bans=frozenset(), limit=200):
    """The items that fit the slot and pass the filters, best score first."""
    text = filters.text.strip().lower()
    found = []
    for entry, item in world.items.items():
        if not fits_slot(item, slot):
            continue
        if filters.class_name and not class_can_wear(item, slot, filters.class_name):
            continue
        if filters.level_min is not None and item.required_level < filters.level_min:
            continue
        if filters.level_max is not None and item.required_level > filters.level_max:
            continue
        if filters.qualities and item.quality not in filters.qualities:
            continue
        if filters.patch is not None and filters.patch not in item.patches:
            continue
        if filters.armor_types and item.armor_type not in filters.armor_types:
            continue
        if text and text not in item.name.lower():
            continue
        sources = sources_in(world, entry, filters.patch)
        if filters.source_kinds:
            kinds = {source.kind for source in sources} or {"none"}
            if not kinds & filters.source_kinds:
                continue
        found.append(Found(item, round(gear_data.score(item, weights), 2), sources, entry in bans))
    found.sort(key=lambda f: (-f.score, f.item.required_level, f.item.entry))
    return found[:limit]


# The classes and roles of `.wowai bot create` (Classes() in src/bots/BotRecipe.cpp).
CLASS_ROLES = {
    "warrior": ("tank", "melee"), "priest": ("healer", "ranged"), "rogue": ("melee",), "warlock": ("ranged",),
    "mage": ("ranged",), "hunter": ("ranged",), "shaman": ("healer", "melee", "ranged"),
    "druid": ("tank", "healer", "melee", "ranged"),
}
LEVELS = (1, 60)
SLOTS = generate.SLOTS
# The positions that a slot fills, as CountedPositions in src/bots/GearTemplate.cpp.
POSITIONS_OF_SLOT = {slot: (slot,) for slot in SLOTS}
POSITIONS_OF_SLOT.update({"mainhand": ("mainhand",), "offhand": ("offhand",), "twohand": ("mainhand", "offhand"),
                          "shield": ("offhand",), "holdable": ("offhand",)})
POSITIONS = [slot for slot in SLOTS if slot not in generate.WEAPON_SLOTS] + ["mainhand", "offhand"]
_NAME = re.compile(r"[a-z0-9_-]+")


@dataclass(frozen=True)
class HandItem:
    slot: str
    entry: int
    min_level: int
    max_level: int
    min_patch: int
    factions: str = "both"   # "both", "horde" or "alliance", as gear_data.FACTION_RACES


@dataclass(frozen=True)
class HandTemplate:
    name: str
    class_name: str
    role: str
    items: list   # [HandItem]


def is_generated_name(name):
    """True for the names of generated templates: <class>_<role>."""
    class_name, _, role = name.partition("_")
    return role in CLASS_ROLES.get(class_name, ())


def _comment_name(world, entry):
    item = world.items.get(entry)
    return " ".join(item.name.replace("#", "").split()) if item else "?"


def render(template, world):
    """The text of the template file."""
    out = ["# Hand-made gear template, from the template builder page (bots/tools/gear/builder_web.py).",
           f"template {template.name} {template.class_name} {template.role} hand"]
    for line in template.items:
        faction = "" if line.factions == "both" else f" {line.factions}"
        out.append(f"item {line.slot} {line.entry} {line.min_level}-{line.max_level} {line.min_patch}{faction}"
                   f"  # {_comment_name(world, line.entry)}")
    return "\n".join(out) + "\n"


def parse(text):
    """Reads a hand-made template file. Raises ValueError for other files."""
    template = None
    items = []
    for number, line in enumerate(text.splitlines(), start=1):
        words = line.split("#", 1)[0].split()
        if not words:
            continue
        if words[0] == "template" and template is None and len(words) == 5:
            if words[4] != "hand":
                raise ValueError("The builder page opens only hand-made templates.")
            template = words[1:4]
        elif words[0] == "item" and template is not None and len(words) in (5, 6) and "-" in words[3]:
            low, _, high = words[3].partition("-")
            factions = words[5] if len(words) == 6 else "both"
            if factions not in ("horde", "alliance") and len(words) == 6:
                raise ValueError(f"line {number}: the faction '{factions}' is not horde or alliance")
            try:
                items.append(HandItem(words[1], int(words[2]), int(low), int(high), int(words[4]), factions))
            except ValueError:
                raise ValueError(f"line {number}: use 'item <slot> <entry> <minLevel>-<maxLevel> <minPatch> "
                                 "[horde|alliance]'")
        else:
            raise ValueError(f"line {number}: not a line of a hand-made template")
    if template is None:
        raise ValueError("no template line")
    return HandTemplate(template[0], template[1], template[2], items)


def check(template, world):
    """The problems that keep mangosd from loading the template. Empty when
    the file is good."""
    problems = []
    if not _NAME.fullmatch(template.name):
        problems.append("A template name has only a-z, 0-9, _ and -.")
    elif is_generated_name(template.name):
        problems.append(f"The name {template.name} is for a generated template. "
                        "Give the hand-made template another name.")
    if template.role not in CLASS_ROLES.get(template.class_name, ()):
        problems.append(f"A {template.class_name} cannot play the role {template.role}.")
    for line in template.items:
        item = world.items.get(line.entry)
        if line.slot not in POSITIONS_OF_SLOT:
            problems.append(f"No slot {line.slot}.")
        elif item is None:
            problems.append(f"No gear item {line.entry} in the item data.")
        elif not fits_slot(item, line.slot):
            problems.append(f"Item {line.entry} ({item.name}) does not fit the slot {line.slot}.")
        if not LEVELS[0] <= line.min_level <= line.max_level <= LEVELS[1]:
            problems.append(f"Levels are from {LEVELS[0]} to {LEVELS[1]}, the lower first: "
                            f"{line.min_level}-{line.max_level}.")
        if line.min_patch not in gear_data.PATCHES:
            problems.append(f"Content patches are from 0 to {gear_data.LAST_PATCH}: {line.min_patch}.")
    if not problems:
        problems += _check_positions(template.items)
    return problems


def _capacity(position):
    return generate.POSITIONS.get(position, 1)


def _check_positions(items):
    # As CheckHandPositions in src/bots/GearTemplate.cpp: items of the same
    # content patch and faction, the error on the first item that overflows a
    # position. A line for both factions counts for the Horde and the Alliance.
    for i, line in enumerate(items):
        for position in POSITIONS_OF_SLOT[line.slot]:
            for faction in ("horde", "alliance"):
                if line.factions not in ("both", faction):
                    continue
                for level in range(line.min_level, line.max_level + 1):
                    count = sum(1 for other in items[:i + 1]
                                if other.min_patch == line.min_patch and other.factions in ("both", faction)
                                and other.min_level <= level <= other.max_level
                                and position in POSITIONS_OF_SLOT[other.slot])
                    if count > _capacity(position):
                        return [f"{'Two' if count == 2 else 'Three'} items fill the position {position} "
                                f"at level {level} and content patch {line.min_patch}."]
    return []


def empty_levels(template, patch=gear_data.LAST_PATCH):
    """position -> [(low, high)]: the level ranges in which the position is
    not full in the content patch. A character wears nothing there."""
    result = {}
    for position in POSITIONS:
        empty = []
        for level in range(LEVELS[0], LEVELS[1] + 1):
            count = sum(1 for line in template.items
                        if line.min_patch <= patch and line.min_level <= level <= line.max_level
                        and position in POSITIONS_OF_SLOT.get(line.slot, ()))
            if count < _capacity(position):
                if empty and empty[-1][1] == level - 1:
                    empty[-1] = (empty[-1][0], level)
                else:
                    empty.append((level, level))
        result[position] = empty
    return result


class SaveError(Exception):
    pass


def _is_hand_made(path):
    try:
        parse(path.read_text())
        return True
    except ValueError:
        return False


def hand_made_names(gear_dir):
    """The names of the hand-made templates in the gear folder."""
    return sorted(path.stem for path in Path(gear_dir).glob("*.txt") if _is_hand_made(path))


def save(gear_dir, template, world):
    """Writes <gear_dir>/<name>.txt. It writes only a good hand-made
    template, and never over a file that is not a hand-made template."""
    problems = check(template, world)
    if problems:
        raise SaveError(" ".join(problems))
    path = Path(gear_dir) / f"{template.name}.txt"
    if path.exists() and not _is_hand_made(path):
        raise SaveError(f"{path.name} is not a hand-made template. The page does not change it.")
    path.write_text(render(template, world))
    return path


def new_line(item):
    """The template line of an item that the page adds: its slot, from its
    required level to 60, from its first content patch. A one-handed weapon
    goes to the main hand."""
    slot = first_slot(item)
    first_patch = min(item.patches) if item.patches else 0
    return HandItem(slot, item.entry, max(item.required_level, LEVELS[0]), LEVELS[1], first_patch)
