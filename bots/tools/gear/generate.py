#!/usr/bin/env python3
"""The gear generator: writes the generated gear templates.

It reads the world database of the local realm through a read-only MySQL
user and writes bots/data/gear/<class>_<role>.txt for each role of
settings.toml. A person reviews the diff and commits it. See step 4 of the
version 1 build spec, "Generator", and bots/data/gear/README.md.

    uv run generate.py [--role warrior_tank ...] [--out DIR]

The database settings are environment variables: WOWAI_GEAR_DB_HOST
(default 127.0.0.1), WOWAI_GEAR_DB_PORT (3306), WOWAI_GEAR_DB_USER
(wowai_gear), WOWAI_GEAR_DB_PASSWORD, and WOWAI_GEAR_DB_NAME (mangos).
The script generate-from-realm makes the user in a test realm and runs this
script with them.
"""

from __future__ import annotations

import argparse
import os
import sys
import tomllib
from dataclasses import dataclass
from pathlib import Path

import gear_data

HERE = Path(__file__).resolve().parent
DEFAULT_OUT = HERE.parent.parent / "data" / "gear"

# The slot words of the format, in the order of the file.
SLOTS = ["head", "neck", "shoulders", "back", "chest", "shirt", "tabard", "wrists", "hands", "waist", "legs",
         "feet", "finger", "trinket", "ranged", "mainhand", "offhand", "twohand", "shield", "holdable"]
WEAPON_SLOTS = ["mainhand", "offhand", "twohand", "shield", "holdable"]
ARMOR_SLOTS = {"head", "shoulders", "chest", "wrists", "hands", "waist", "legs", "feet"}
POSITIONS = {"finger": 2, "trinket": 2}

# What each class can learn to wear and wield: armor type -> the first level.
# Facts of the game, not settings. All classes wear cloaks and the "misc"
# items (rings, necks, trinkets, off-hand items).
CLASSES = {
    "warrior": ({"cloth": 1, "leather": 1, "mail": 1, "plate": 40, "shield": 1},
                {"axe", "axe2h", "mace", "mace2h", "polearm", "sword", "sword2h", "staff", "fist", "dagger",
                 "bow", "gun", "crossbow", "thrown"}),
    "paladin": ({"cloth": 1, "leather": 1, "mail": 1, "plate": 40, "shield": 1},
                {"axe", "axe2h", "mace", "mace2h", "polearm", "sword", "sword2h"}),
    "hunter": ({"cloth": 1, "leather": 1, "mail": 40},
               {"axe", "axe2h", "polearm", "sword", "sword2h", "staff", "fist", "dagger", "bow", "gun", "crossbow",
                "thrown"}),
    "rogue": ({"cloth": 1, "leather": 1},
              {"dagger", "sword", "mace", "fist", "bow", "gun", "crossbow", "thrown"}),
    "priest": ({"cloth": 1}, {"mace", "staff", "dagger", "wand"}),
    "shaman": ({"cloth": 1, "leather": 1, "mail": 40, "shield": 1},
               {"axe", "axe2h", "mace", "mace2h", "staff", "dagger", "fist"}),
    "mage": ({"cloth": 1}, {"staff", "sword", "dagger", "wand"}),
    "warlock": ({"cloth": 1}, {"staff", "sword", "dagger", "wand"}),
    "druid": ({"cloth": 1, "leather": 1}, {"mace", "mace2h", "staff", "dagger", "fist"}),
}


@dataclass(frozen=True)
class Role:
    name: str
    class_name: str
    role: str
    shapes: list      # [(name, [weapon slot, ...])]
    weapons: dict     # slot -> set of weapon types (mainhand, offhand, twohand, ranged)
    weights: dict


@dataclass(frozen=True)
class Settings:
    min_level: int
    max_level: int
    window: int
    source_rules: gear_data.SourceRules
    min_quality: int
    epic_min_level: int
    blue_caps: list   # [(from, to, most)]
    pool_size: int
    roles: dict       # name -> Role

    @staticmethod
    def load(path):
        with open(path, "rb") as file:
            return Settings.from_dict(tomllib.load(file))

    @staticmethod
    def from_dict(data):
        roles = {}
        for name, entry in data["roles"].items():
            class_name, _, role = name.partition("_")
            if class_name not in CLASSES:
                raise ValueError(f"role {name}: unknown class {class_name}")
            _, class_weapons = CLASSES[class_name]
            shapes = [(shape[0], list(shape[1:])) for shape in entry["shapes"]]
            weapons = {slot: set(types) for slot, types in entry.get("weapons", {}).items()}
            for slot, types in weapons.items():
                if slot not in WEAPON_SLOTS + ["ranged"]:
                    raise ValueError(f"role {name}: {slot} is not a weapon slot")
                for weapon_type in types:
                    if weapon_type not in class_weapons:
                        raise ValueError(f"role {name}: a {class_name} cannot use {weapon_type}")
            for shape_name, slots in shapes:
                for slot in slots:
                    if slot not in WEAPON_SLOTS:
                        raise ValueError(f"role {name}, shape {shape_name}: {slot} is not a weapon slot")
                    if slot in ("mainhand", "offhand", "twohand") and slot not in weapons:
                        raise ValueError(f"role {name}, shape {shape_name}: no weapon types for {slot}")
            roles[name] = Role(name, class_name, role, shapes, weapons, dict(entry["weights"]))
        sources = data["sources"]
        return Settings(
            min_level=data["levels"]["min"], max_level=data["levels"]["max"],
            window=data["levels"]["required_level_window"],
            source_rules=gear_data.SourceRules(sources["creature_level_margin"], sources["world_drop_level_margin"],
                                               sources["include_raids"]),
            min_quality=data["quality"]["min"], epic_min_level=data["quality"]["epic_min_level"],
            blue_caps=[tuple(cap) for cap in data["quality"]["blue_caps"]], pool_size=data["pool"]["size"],
            roles=roles)

    def blue_cap(self, level):
        for low, high, most in self.blue_caps:
            if low <= level <= high:
                return most
        return 0


@dataclass(frozen=True)
class Line:
    """One `item` line of a generated template."""
    slot: str
    entry: int
    min_level: int
    max_level: int
    min_patch: int
    score: float


def shapes_of(settings, role_name):
    return settings.roles[role_name].shapes


def _slots_for(item, role):
    """The slots of the template that the item can fill for the role."""
    used = {slot for _, slots in role.shapes for slot in slots}
    if item.slot == "onehand":
        candidates = ["mainhand", "offhand"]
    else:
        candidates = [item.slot]
    result = []
    for slot in candidates:
        if slot in WEAPON_SLOTS and slot not in used:
            continue
        if slot in role.weapons and item.weapon_type not in role.weapons[slot]:
            continue
        if slot == "ranged" and item.weapon_type not in role.weapons.get("ranged", set()):
            continue
        if slot in ("mainhand", "offhand", "twohand") and not item.weapon_type:
            continue
        result.append(slot)
    return result


def _armor_level(item, slot, class_name):
    """The first level at which the class wears the item, or None if never."""
    armor_levels, _ = CLASSES[class_name]
    if slot in ARMOR_SLOTS or slot == "shield":
        return armor_levels.get(item.armor_type)
    return 1


def _candidate_lines(world, settings, role, bans):
    class_id = gear_data.CLASS_IDS[role.class_name]
    lines = []
    for entry, item in world.items.items():
        if entry in bans or item.excluded or not item.usable_by(class_id) or not item.usable_by_horde():
            continue
        if item.quality < settings.min_quality or item.quality > 4:
            continue
        item_score = round(gear_data.score(item, role.weights), 2)
        if item_score <= 0:
            continue
        sources = world.sources.get(entry, [])
        for slot in _slots_for(item, role):
            armor_level = _armor_level(item, slot, role.class_name)
            if armor_level is None:
                continue
            if slot == "offhand" and item.unique:
                continue  # The main hand can hold the same item.
            low = max(item.required_level, settings.min_level, armor_level)
            if item.quality == 4:
                low = max(low, settings.epic_min_level)
            high = min(item.required_level + settings.window, settings.max_level)
            # The lowest level per patch. An item counts in a patch only if
            # it stays in every later patch, so the levels only go down.
            lows = []
            for patch in gear_data.PATCHES:
                level = gear_data.earliest_level(sources, patch, class_id, settings.source_rules)
                lows.append(None if level is None or patch not in item.patches else max(low, level))
            later = None
            for patch in reversed(gear_data.PATCHES):
                if lows[patch] is None:
                    later = "gone"
                if later == "gone":
                    lows[patch] = None
                else:
                    later = lows[patch] if later is None else max(later, lows[patch])
                    lows[patch] = later
            previous = None
            for patch in gear_data.PATCHES:
                level = lows[patch]
                if level is None or level > high or (previous is not None and level >= previous):
                    continue
                lines.append(Line(slot, entry, level, high, patch, item_score))
                previous = level
    return lines


def _apply_blue_caps(world, settings, lines):
    """Keeps the best blue items while the blue items that a character can
    wear at each level stay within the caps. A blue item may lose levels."""
    kept = [line for line in lines if world.items[line.entry].quality != 3]
    blues = sorted((line for line in lines if world.items[line.entry].quality == 3),
                   key=lambda line: (-line.score, line.entry, line.min_patch))
    worn = {}  # (slot, level) -> set of blue entries

    def count(level, extra=None):
        total = 0
        for slot in SLOTS:
            entries = set(worn.get((slot, level), set()))
            if extra and extra[0] == slot:
                entries.add(extra[1])
            total += min(len(entries), POSITIONS.get(slot, 1))
        return total

    for line in blues:
        allowed = [level for level in range(line.min_level, line.max_level + 1)
                   if count(level, (line.slot, line.entry)) <= settings.blue_cap(level)]
        runs, run = [], []
        for level in allowed:
            if run and level != run[-1] + 1:
                runs.append(run)
                run = []
            run.append(level)
        if run:
            runs.append(run)
        if not runs:
            continue
        best = max(runs, key=len)
        for level in best:
            worn.setdefault((line.slot, level), set()).add(line.entry)
        kept.append(Line(line.slot, line.entry, best[0], best[-1], line.min_patch, line.score))
    return kept


def _prune(settings, lines):
    """Drops the lines that are never in the top of a pool."""
    used = set()
    for slot in SLOTS:
        of_slot = [line for line in lines if line.slot == slot]
        if not of_slot:
            continue
        for patch in gear_data.PATCHES:
            for level in range(settings.min_level, settings.max_level + 1):
                allowed = [line for line in of_slot
                           if line.min_level <= level <= line.max_level and line.min_patch <= patch]
                best = {}
                for line in allowed:
                    best[line.entry] = max(best.get(line.entry, 0), line.score)
                top = sorted(best, key=lambda entry: (-best[entry], entry))[:settings.pool_size]
                used.update(id(line) for line in allowed if line.entry in top)
    return [line for line in lines if id(line) in used]


def build_lines(world, settings, role_name, bans):
    """The item lines of the generated template of a role."""
    role = settings.roles[role_name]
    lines = _candidate_lines(world, settings, role, bans)
    lines = _apply_blue_caps(world, settings, lines)
    lines = _prune(settings, lines)
    return sorted(lines, key=lambda line: (SLOTS.index(line.slot), line.min_level, line.entry, line.min_patch))


def _source_word(world, entry, patch):
    sources = [source for source in world.sources.get(entry, []) if source.levels[patch] is not None]
    if not sources:
        return "?"
    best = min(sources, key=lambda source: source.levels[patch])
    return best.kind


def render(world, settings, role_name, lines):
    role = settings.roles[role_name]
    out = [
        "# Generated by bots/tools/gear/generate.py from the world database, with",
        "# bots/tools/gear/settings.toml and bots/tools/gear/bans.txt. Do not edit:",
        "# change the settings or the ban file and run the generator again.",
        f"template {role_name} {role.class_name} {role.role} generated",
    ]
    for name, slots in role.shapes:
        out.append(f"shape {name} {' '.join(slots)}")
    for line in lines:
        item = world.items[line.entry]
        name = " ".join(item.name.split()).replace("#", "")
        out.append(f"item {line.slot} {line.entry} {line.min_level}-{line.max_level} {line.min_patch} "
                   f"{line.score:.2f}  # {name}, {item.quality_name}, {_source_word(world, line.entry, line.min_patch)}")
    return "\n".join(out) + "\n"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--role", action="append", help="a role of settings.toml, for example warrior_tank")
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT, help="the gear folder (default: bots/data/gear)")
    parser.add_argument("--settings", type=Path, default=HERE / "settings.toml")
    parser.add_argument("--bans", type=Path, default=HERE / "bans.txt")
    args = parser.parse_args(argv)

    settings = Settings.load(args.settings)
    bans = gear_data.read_bans(args.bans)
    roles = args.role or list(settings.roles)
    for name in roles:
        if name not in settings.roles:
            parser.error(f"no role {name} in {args.settings}")

    env = os.environ
    connection = gear_data.connect(env.get("WOWAI_GEAR_DB_HOST", "127.0.0.1"), int(env.get("WOWAI_GEAR_DB_PORT", "3306")),
                                   env.get("WOWAI_GEAR_DB_USER", "wowai_gear"), env.get("WOWAI_GEAR_DB_PASSWORD", ""),
                                   env.get("WOWAI_GEAR_DB_NAME", "mangos"))
    try:
        world = gear_data.load_world(connection)
    finally:
        connection.close()

    args.out.mkdir(parents=True, exist_ok=True)
    for name in roles:
        lines = build_lines(world, settings, name, bans)
        path = args.out / f"{name}.txt"
        path.write_text(render(world, settings, name, lines))
        print(f"{path}: {len(lines)} items")
    return 0


if __name__ == "__main__":
    sys.exit(main())
