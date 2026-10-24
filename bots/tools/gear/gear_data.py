"""Item data of the world database for the gear tools.

The generator (generate.py) and the template builder page share this module.
It holds the item data, the sources of each item, the level of each source,
the content patch rules, the stat weights score, and the ban list. See step 4
of the version 1 build spec.

A content patch is a number as the core counts it (`WowPatch` in
mangosd.conf): 0 = 1.2, 1 = 1.3, ..., 10 = 1.12.

load_world() reads the world database through a read-only MySQL user.
build_world() turns the rows into Item and Source objects, so that the tests
can give it made-up rows.
"""

from __future__ import annotations

import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

PATCHES = range(0, 11)
LAST_PATCH = PATCHES[-1]
CLIENT_BUILD = 5875

# The Horde races: orc, undead, tauren, troll. Bots are Horde.
HORDE_RACES = (1 << 1) | (1 << 4) | (1 << 5) | (1 << 7)

QUALITY_NAMES = {0: "grey", 1: "white", 2: "green", 3: "blue", 4: "epic", 5: "legendary", 6: "artifact"}

CLASS_IDS = {"warrior": 1, "paladin": 2, "hunter": 3, "rogue": 4, "priest": 5, "shaman": 7,
             "mage": 8, "warlock": 9, "druid": 11}

# item_template.inventory_type -> the slot word of the gear template format.
SLOT_OF_INVENTORY_TYPE = {
    1: "head", 2: "neck", 3: "shoulders", 4: "shirt", 5: "chest", 6: "waist", 7: "legs", 8: "feet",
    9: "wrists", 10: "hands", 11: "finger", 12: "trinket", 13: "onehand", 14: "shield", 15: "ranged",
    16: "back", 17: "twohand", 19: "tabard", 20: "chest", 21: "mainhand", 22: "offhand",
    23: "holdable", 25: "ranged", 26: "ranged",
}
# "onehand" is not a slot of the format: a one-handed weapon fits the
# mainhand slot and the offhand slot.

WEAPON_TYPES = {0: "axe", 1: "axe2h", 2: "bow", 3: "gun", 4: "mace", 5: "mace2h", 6: "polearm",
                7: "sword", 8: "sword2h", 10: "staff", 13: "fist", 15: "dagger", 16: "thrown",
                18: "crossbow", 19: "wand"}
ARMOR_TYPES = {0: "misc", 1: "cloth", 2: "leather", 3: "mail", 4: "plate", 6: "shield"}

STAT_NAMES = {0: "mana", 1: "health", 3: "agility", 4: "strength", 5: "intellect", 6: "spirit", 7: "stamina"}
SCHOOL_NAMES = {2: "holy", 4: "fire", 8: "nature", 16: "frost", 32: "shadow", 64: "arcane"}
ALL_MAGIC = 126
DEFENSE_SKILL = 95

# Spell auras of equip spells, as stat names. Auras 13, 135, 22, and 30 are
# read in _spell_stats.
SIMPLE_AURAS = {
    99: "attack_power", 124: "ranged_attack_power", 52: "crit", 54: "hit", 57: "spell_crit",
    71: "spell_crit", 55: "spell_hit", 49: "dodge", 47: "parry", 51: "block", 158: "block_value",
}
AURA_MOD_DAMAGE_DONE = 13
AURA_MOD_HEALING_DONE = 135
AURA_MOD_POWER_REGEN = 85
AURA_MOD_RESISTANCE = 22
AURA_MOD_SKILL = 30
ITEM_EXTRA_NOT_OBTAINABLE = 0x04
SPELL_TRIGGER_ON_EQUIP = 1


@dataclass(frozen=True)
class Item:
    entry: int
    name: str
    quality: int
    item_class: int
    subclass: int
    slot: str             # A slot word of the format, or "onehand". "" for no gear slot.
    weapon_type: str      # "" when the item is not a weapon.
    armor_type: str       # "" when the item is not armor.
    required_level: int
    allowable_class: int
    allowable_race: int
    stats: dict
    armor: int
    dps: float
    patches: frozenset    # The content patches in which the item exists.
    excluded: str         # Why no generated pool may hold the item, "" if none.
    unique: bool

    @property
    def quality_name(self):
        return QUALITY_NAMES.get(self.quality, str(self.quality))

    def usable_by(self, class_id):
        return self.allowable_class in (-1, 0) or bool(self.allowable_class & (1 << (class_id - 1)))

    def usable_by_horde(self):
        return self.allowable_race in (-1, 0) or bool(self.allowable_race & HORDE_RACES)


@dataclass(frozen=True)
class Source:
    """One way to get an item: a quest, creature loot, a vendor, or a recipe.

    kind: quest, creature (loot of an open-world creature), world (a world
    drop: loot of an open-world creature through a reference table),
    dungeon, raid, vendor, or craft. levels[patch] is the level of the source
    in that content patch, or None when the source is not there. For loot it
    is the lowest level of the creatures that drop it, and for a dungeon or a
    raid the lowest creature level on its map. Vendor and craft sources have
    level 0: the item's required level is their level.
    """
    kind: str
    name: str
    levels: tuple
    classes: int = 0      # Class mask of a class quest. 0 for all classes.


@dataclass(frozen=True)
class SourceRules:
    creature_level_margin: int = 0    # Creature loot counts up to this many levels above L.
    world_drop_level_margin: int = 3  # A world drop counts up to this many levels above L.
    include_raids: bool = False


@dataclass
class World:
    items: dict = field(default_factory=dict)    # entry -> Item
    sources: dict = field(default_factory=dict)  # entry -> [Source]


def earliest_level(sources, patch, class_id, rules):
    """The lowest character level at which one of the sources counts in the
    content patch, or None. The item's required level is not part of it."""
    best = None
    for source in sources:
        level = source.levels[patch]
        if level is None or (source.classes and not source.classes & (1 << (class_id - 1))):
            continue
        if source.kind == "creature":
            level -= rules.creature_level_margin
        elif source.kind == "world":
            level -= rules.world_drop_level_margin
        elif source.kind == "raid" and not rules.include_raids:
            continue
        level = max(level, 0)
        best = level if best is None else min(best, level)
    return best


def score(item, weights):
    """The weighted stats, plus armor, plus damage per second for weapons."""
    total = sum(weights.get(name, 0.0) * value for name, value in item.stats.items())
    total += weights.get("armor", 0.0) * item.armor
    if item.dps:
        total += weights.get("wand_dps" if item.weapon_type == "wand" else "dps", 0.0) * item.dps
    return total


def read_bans(path):
    """The entries of the ban file: one `ban <entry>` line per item."""
    bans = set()
    for number, line in enumerate(Path(path).read_text().splitlines(), start=1):
        words = line.split("#", 1)[0].split()
        if not words:
            continue
        if len(words) != 2 or words[0] != "ban" or not words[1].isdigit():
            raise ValueError(f"{path}, line {number}: use 'ban <entry>'")
        bans.add(int(words[1]))
    return bans


# Building the world from rows.

def _patch_levels(patches, level):
    return tuple(level if patch in patches else None for patch in PATCHES)


def _patch_range(low, high):
    return frozenset(patch for patch in PATCHES if low <= patch <= high)


def _merge_levels(a, b):
    return tuple(y if x is None else x if y is None else min(x, y) for x, y in zip(a, b))


def _spell_stats(spell, stats):
    for i in (1, 2, 3):
        aura = spell[f"aura{i}"]
        value = spell[f"base{i}"] + spell[f"dice{i}"]
        misc = spell[f"misc{i}"]
        names = []
        if aura == AURA_MOD_DAMAGE_DONE:
            if misc & ALL_MAGIC == ALL_MAGIC:
                names = ["spell_damage", "healing"]
            else:
                names = [f"{SCHOOL_NAMES[mask]}_damage" for mask in SCHOOL_NAMES if misc & mask]
        elif aura == AURA_MOD_HEALING_DONE:
            names = ["healing"]
        elif aura == AURA_MOD_POWER_REGEN and misc == 0:
            names = ["mp5"]
        elif aura == AURA_MOD_RESISTANCE and misc == 1:
            names = ["armor_bonus"]
        elif aura == AURA_MOD_SKILL and misc == DEFENSE_SKILL:
            names = ["defense"]
        elif aura in SIMPLE_AURAS:
            names = [SIMPLE_AURAS[aura]]
        for name in names:
            stats[name] = stats.get(name, 0) + value


def _excluded(row):
    if row["extra_flags"] & ITEM_EXTRA_NOT_OBTAINABLE:
        return "not obtainable"
    if row["required_honor_rank"] or row["required_city_rank"]:
        return "needs an honor rank"
    if row["required_reputation_faction"]:
        return "needs a reputation"
    if row["random_property"]:
        return "random suffix"
    if row["required_skill"] or row["required_spell"]:
        return "needs a skill or a spell"
    return ""


def _forbidden(forbidden_rows):
    """entry -> the patches in which forbidden_items removes it, as the core
    reads that table for loot and vendors."""
    result = {}
    for row in forbidden_rows:
        if row["after_or_before"] == 0:
            patches = _patch_range(row["patch"], LAST_PATCH)
        else:
            patches = _patch_range(0, row["patch"])
        result[row["entry"]] = result.get(row["entry"], frozenset()) | patches
    return result


def _build_items(item_rows, spells, forbidden):
    rows_of = {}
    for row in item_rows:
        rows_of.setdefault(row["entry"], []).append(row)
    items = {}
    for entry, rows in rows_of.items():
        rows.sort(key=lambda row: row["patch"])
        row = rows[-1]
        shape = (row["class"], row["subclass"], row["inventory_type"])
        # The core loads the newest row at or below the realm's patch. The
        # item counts in a patch only if that row has the slot of the newest.
        patches = set()
        for patch in PATCHES:
            older = [r for r in rows if r["patch"] <= patch]
            if older and (older[-1]["class"], older[-1]["subclass"], older[-1]["inventory_type"]) == shape:
                patches.add(patch)
        patches -= forbidden.get(entry, frozenset())

        stats = {}
        for i in range(1, 11):
            name = STAT_NAMES.get(row[f"stat_type{i}"])
            if name and row[f"stat_value{i}"]:
                stats[name] = stats.get(name, 0) + row[f"stat_value{i}"]
        for i in range(1, 6):
            spell = spells.get(row[f"spellid_{i}"])
            if spell and row[f"spelltrigger_{i}"] == SPELL_TRIGGER_ON_EQUIP:
                _spell_stats(spell, stats)
        if row["block"]:
            stats["block_value"] = stats.get("block_value", 0) + row["block"]

        dps = 0.0
        if row["class"] == 2 and row["delay"]:
            damage = sum(row[f"dmg_min{i}"] + row[f"dmg_max{i}"] for i in range(1, 6)) / 2
            dps = damage / (row["delay"] / 1000.0)

        items[entry] = Item(
            entry=entry, name=row["name"], quality=row["quality"], item_class=row["class"],
            subclass=row["subclass"], slot=SLOT_OF_INVENTORY_TYPE.get(row["inventory_type"], ""),
            weapon_type=WEAPON_TYPES.get(row["subclass"], "") if row["class"] == 2 else "",
            armor_type=ARMOR_TYPES.get(row["subclass"], "") if row["class"] == 4 else "",
            required_level=row["required_level"], allowable_class=row["allowable_class"],
            allowable_race=row["allowable_race"], stats=stats, armor=row["armor"], dps=dps,
            patches=frozenset(patches), excluded=_excluded(row), unique=row["max_count"] == 1)
    return items


def _creature_contexts(tables):
    """For each creature entry: [(kind, name, patches, level)], one per map it
    spawns on. kind is creature, dungeon, or raid."""
    maps = {row["entry"]: row for row in tables["maps"]}
    creature_patch = {}
    creatures = {}
    for row in tables["creatures"]:
        creature_patch[row["entry"]] = min(creature_patch.get(row["entry"], LAST_PATCH), row["patch"])
        creatures[row["entry"]] = row
    map_level = {}
    spawn_patches = {}
    for row in tables["spawns"]:
        creature = creatures.get(row["id"])
        if not creature:
            continue
        level = creature["level_min"]
        map_level[row["map"]] = min(map_level.get(row["map"], level), level)
        key = (row["id"], row["map"])
        spawn_patches[key] = spawn_patches.get(key, frozenset()) | _patch_range(row["patch_min"], row["patch_max"])

    contexts = {}
    for (entry, map_id), patches in spawn_patches.items():
        map_row = maps.get(map_id, {"map_type": 0, "name": str(map_id)})
        patches &= _patch_range(creature_patch[entry], LAST_PATCH)
        creature = creatures[entry]
        if map_row["map_type"] == 1:
            context = ("dungeon", map_row["name"], patches, map_level[map_id])
        elif map_row["map_type"] == 2:
            context = ("raid", map_row["name"], patches, map_level[map_id])
        elif map_row["map_type"] == 0:
            context = ("creature", creature["name"], patches, creature["level_min"])
        else:
            continue  # Battlegrounds.
        contexts.setdefault(entry, []).append(context)
    return creatures, contexts


class _LootSources:
    """Turns the loot tables into sources: kind -> (name, levels) per item."""

    def __init__(self, tables):
        self.creature_rows = {}
        for row in tables["creature_loot"]:
            self.creature_rows.setdefault(row["entry"], []).append(row)
        self.reference_rows = {}
        for row in tables["reference_loot"]:
            self.reference_rows.setdefault(row["entry"], []).append(row)
        self.found = {}  # item -> {(kind): (name, levels)}

    def add(self, item, kind, name, levels):
        if all(level is None for level in levels):
            return
        known = self.found.setdefault(item, {})
        if kind not in known:
            known[kind] = (name, levels)
            return
        old_name, old_levels = known[kind]
        best_old = min((level for level in old_levels if level is not None), default=999)
        best_new = min(level for level in levels if level is not None)
        known[kind] = (name if best_new < best_old else old_name, _merge_levels(old_levels, levels))

    def walk(self, rows, kind, name, levels, depth=0):
        if depth > 5:
            return
        for row in rows:
            row_levels = tuple(level if row["patch_min"] <= patch <= row["patch_max"] else None
                               for patch, level in zip(PATCHES, levels))
            if row["mincountOrRef"] < 0:
                reference = self.reference_rows.get(-row["mincountOrRef"], [])
                self.walk(reference, "world" if kind == "creature" else kind, name, row_levels, depth + 1)
            else:
                self.add(row["item"], kind, name, row_levels)


def build_world(tables):
    spells = {row["entry"]: row for row in tables["spells"]}
    forbidden = _forbidden(tables["forbidden"])
    items = _build_items(tables["items"], spells, forbidden)
    sources = {}

    def add(entry, source):
        if entry in items:
            sources.setdefault(entry, []).append(source)

    for row in tables["quests"]:
        if row["RequiredRaces"] and not row["RequiredRaces"] & HORDE_RACES:
            continue
        level = row["QuestLevel"] if row["QuestLevel"] > 0 else row["MinLevel"]
        levels = _patch_levels(_patch_range(row["patch"], LAST_PATCH), level)
        for entry in set(row["items"]):
            add(entry, Source("quest", row["Title"], levels, row["RequiredClasses"]))

    creatures, contexts = _creature_contexts(tables)
    loot = _LootSources(tables)
    for entry, creature in creatures.items():
        rows = loot.creature_rows.get(creature["loot_id"]) if creature["loot_id"] else None
        if not rows:
            continue
        for kind, name, patches, level in contexts.get(entry, []):
            loot.walk(rows, kind, name, _patch_levels(patches, level))
    for entry, kinds in loot.found.items():
        for kind, (name, levels) in kinds.items():
            add(entry, Source(kind, name, levels))

    vendor_patches = {}
    for row in tables["vendors"]:
        patches = frozenset()
        for _, name, context_patches, _ in contexts.get(row["creature"], []):
            patches |= context_patches
        if patches:
            vendor_patches[row["item"]] = vendor_patches.get(row["item"], frozenset()) | patches
    for entry, patches in vendor_patches.items():
        add(entry, Source("vendor", "a vendor", _patch_levels(patches, 0)))
    for row in tables["crafts"]:
        add(row["item"], Source("craft", row["name"], _patch_levels(frozenset(PATCHES), 0)))

    # Sources count only in the patches in which the item exists.
    for entry, item_sources in sources.items():
        patches = items[entry].patches
        sources[entry] = [Source(s.kind, s.name, tuple(level if patch in patches else None
                                                        for patch, level in zip(PATCHES, s.levels)), s.classes)
                          for s in item_sources]
    return World(items=items, sources=sources)


# Reading the world database.

GEAR_ITEMS = "class IN (2, 4) AND inventory_type > 0"

QUERIES = {
    "items": f"""
        SELECT entry, patch, class, subclass, name, quality, flags, extra_flags, inventory_type,
               allowable_class, allowable_race, required_level, required_skill, required_spell,
               required_honor_rank, required_city_rank, required_reputation_faction, max_count,
               random_property, armor, block, delay,
               {", ".join(f"stat_type{i}, stat_value{i}" for i in range(1, 11))},
               {", ".join(f"dmg_min{i}, dmg_max{i}" for i in range(1, 6))},
               {", ".join(f"spellid_{i}, spelltrigger_{i}" for i in range(1, 6))}
        FROM item_template WHERE {GEAR_ITEMS}""",
    # Every build of the aura spells. read_tables keeps the newest build.
    "spells": f"""
        SELECT entry, build,
               {", ".join(f"effectApplyAuraName{i} AS aura{i}, effectBasePoints{i} AS base{i}, "
                          f"effectBaseDice{i} AS dice{i}, effectMiscValue{i} AS misc{i}" for i in (1, 2, 3))}
        FROM spell_template
        WHERE build <= {CLIENT_BUILD} AND (effect1 = 6 OR effect2 = 6 OR effect3 = 6)""",
    "forbidden": "SELECT entry, patch, after_or_before FROM forbidden_items",
    "quests": f"""
        SELECT entry, MIN(patch) AS patch, QuestLevel, MinLevel, RequiredRaces, RequiredClasses, Title,
               RewChoiceItemId1, RewChoiceItemId2, RewChoiceItemId3, RewChoiceItemId4, RewChoiceItemId5,
               RewChoiceItemId6, RewItemId1, RewItemId2, RewItemId3, RewItemId4
        FROM quest_template q
        WHERE (entry IN (SELECT quest FROM creature_questrelation)
               OR entry IN (SELECT quest FROM gameobject_questrelation)
               OR entry IN (SELECT start_quest FROM item_template WHERE start_quest > 0))
        GROUP BY entry""",
    "creatures": "SELECT entry, MIN(patch) AS patch, name, level_min, loot_id FROM creature_template GROUP BY entry",
    "spawns": """
        SELECT id, map, MIN(patch_min) AS patch_min, MAX(patch_max) AS patch_max FROM (
            SELECT id, map, patch_min, patch_max FROM creature WHERE id > 0
            UNION ALL SELECT id2, map, patch_min, patch_max FROM creature WHERE id2 > 0
            UNION ALL SELECT id3, map, patch_min, patch_max FROM creature WHERE id3 > 0
            UNION ALL SELECT id4, map, patch_min, patch_max FROM creature WHERE id4 > 0
            UNION ALL SELECT id5, map, patch_min, patch_max FROM creature WHERE id5 > 0) spawns
        GROUP BY id, map""",
    "maps": "SELECT entry, map_type, map_name AS name FROM map_template GROUP BY entry",
    "creature_loot": f"""
        SELECT entry, item, mincountOrRef, patch_min, patch_max FROM creature_loot_template
        WHERE mincountOrRef < 0 OR item IN (SELECT entry FROM item_template WHERE {GEAR_ITEMS})""",
    "reference_loot": f"""
        SELECT entry, item, mincountOrRef, patch_min, patch_max FROM reference_loot_template
        WHERE mincountOrRef < 0 OR item IN (SELECT entry FROM item_template WHERE {GEAR_ITEMS})""",
    "vendors": f"""
        SELECT entry AS creature, item FROM npc_vendor
        UNION SELECT c.entry AS creature, v.item FROM npc_vendor_template v
              JOIN creature_template c ON c.vendor_id = v.entry""",
    "crafts": f"""
        SELECT DISTINCT r.effectItemType1 AS item, r.name FROM spell_template r
        JOIN spell_template t ON t.effect1 = 36 AND t.effectTriggerSpell1 = r.entry
        WHERE r.effect1 = 24 AND r.effectItemType1 > 0
          AND t.entry IN (SELECT spell FROM npc_trainer WHERE {CLIENT_BUILD} BETWEEN build_min AND build_max
                          UNION SELECT spell FROM npc_trainer_template
                                WHERE {CLIENT_BUILD} BETWEEN build_min AND build_max)""",
}


def read_tables(connection):
    """The rows that build_world needs, from the world database."""
    tables = {}
    with connection.cursor() as cursor:
        for name, sql in QUERIES.items():
            started = time.monotonic()
            cursor.execute(sql)
            columns = [column[0] for column in cursor.description]
            tables[name] = [dict(zip(columns, row)) for row in cursor.fetchall()]
            print(f"Read {len(tables[name])} rows of {name} in {time.monotonic() - started:.1f} s", file=sys.stderr)
    newest = {}
    for row in tables["spells"]:
        if row["entry"] not in newest or row["build"] > newest[row["entry"]]["build"]:
            newest[row["entry"]] = row
    tables["spells"] = list(newest.values())
    for row in tables["quests"]:
        row["items"] = [row[f"RewChoiceItemId{i}"] for i in range(1, 7)] + [row[f"RewItemId{i}"] for i in range(1, 5)]
        row["items"] = [entry for entry in row["items"] if entry]
    return tables


def connect(host, port, user, password, database):
    import pymysql  # Only the database reads need it, not the tests.
    return pymysql.connect(host=host, port=port, user=user, password=password, database=database,
                           charset="utf8mb4", read_timeout=600)


def load_world(connection):
    return build_world(read_tables(connection))
