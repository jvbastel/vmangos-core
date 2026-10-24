"""Small made-up world database tables for the tests.

The rows have the columns that gear_data.load_world reads from the world
database. Each helper fills the columns that a test does not care about.
"""


def item(entry, name="Item", patch=0, item_class=4, subclass=1, inventory_type=5, quality=2,
         required_level=10, stats=None, armor=0, dmg=(0, 0), delay=0, block=0, spells=(),
         allowable_class=-1, allowable_race=-1, flags=0, extra_flags=0, max_count=0,
         random_property=0, required_honor_rank=0, required_reputation_faction=0,
         required_skill=0, required_spell=0, required_city_rank=0):
    row = {
        "entry": entry, "patch": patch, "class": item_class, "subclass": subclass, "name": name,
        "quality": quality, "flags": flags, "extra_flags": extra_flags, "inventory_type": inventory_type,
        "allowable_class": allowable_class, "allowable_race": allowable_race,
        "required_level": required_level, "required_skill": required_skill, "required_spell": required_spell,
        "required_honor_rank": required_honor_rank, "required_city_rank": required_city_rank,
        "required_reputation_faction": required_reputation_faction, "max_count": max_count,
        "random_property": random_property, "armor": armor, "block": block, "delay": delay,
    }
    stats = list((stats or {}).items())
    for i in range(1, 11):
        stat_type, value = stats[i - 1] if i <= len(stats) else (0, 0)
        row[f"stat_type{i}"] = stat_type
        row[f"stat_value{i}"] = value
    for i in range(1, 6):
        row[f"dmg_min{i}"] = dmg[0] if i == 1 else 0
        row[f"dmg_max{i}"] = dmg[1] if i == 1 else 0
        spell = spells[i - 1] if i <= len(spells) else 0
        row[f"spellid_{i}"] = spell
        row[f"spelltrigger_{i}"] = 1 if spell else 0
    return row


def spell(entry, aura, value, misc=0):
    return {"entry": entry, "aura1": aura, "base1": value - 1, "dice1": 1, "misc1": misc,
            "aura2": 0, "base2": 0, "dice2": 0, "misc2": 0, "aura3": 0, "base3": 0, "dice3": 0, "misc3": 0}


def quest(entry, rewards, level=10, min_level=8, patch=0, races=0, classes=0, title="A quest"):
    return {"entry": entry, "patch": patch, "QuestLevel": level, "MinLevel": min_level,
            "RequiredRaces": races, "RequiredClasses": classes, "Title": title, "items": rewards}


def creature(entry, level, loot_id, name="A creature", patch=0):
    return {"entry": entry, "patch": patch, "name": name, "level_min": level, "loot_id": loot_id}


def spawn(creature_entry, map_id=1, patch_min=0, patch_max=10):
    return {"id": creature_entry, "map": map_id, "patch_min": patch_min, "patch_max": patch_max}


def loot(entry, item_entry, ref=0, patch_min=0, patch_max=10):
    return {"entry": entry, "item": item_entry, "mincountOrRef": -ref if ref else 1,
            "patch_min": patch_min, "patch_max": patch_max}


def tables(**given):
    names = ["items", "spells", "forbidden", "quests", "creatures", "spawns", "maps",
             "creature_loot", "reference_loot", "vendors", "crafts"]
    result = {name: [] for name in names}
    result["maps"] = [{"entry": 0, "map_type": 0, "name": "Eastern Kingdoms"},
                      {"entry": 1, "map_type": 0, "name": "Kalimdor"}]
    for name, rows in given.items():
        assert name in names, name
        result[name] = list(rows) if name != "maps" else result["maps"] + list(rows)
    return result
