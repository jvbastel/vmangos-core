#!/usr/bin/env python3
"""Reads the Forever tooltip of items from Wowhead's tooltip service, with a cache, and prints what the loot
rows of the spec need (spec jvbastel/WOWAI#240, #242).

    uv run python tools/wowhead_tooltips.py [--cache DIR] [--refetch] <item ID> ...

The only Wowhead address a script may read is the tooltip service
`https://nether.wowhead.com/tooltip/item/<id>?dataEnv=16&locale=0` (dataEnv 16 = Forever). The www.wowhead.com
pages answer 403 to scripts: do not work around that. Quest reward lists come from the quest page
(`https://www.wowhead.com/forever/quest=<id>`) read by hand or with a web-fetch tool, not from this script.

The raw answer of each item goes into the cache folder (default notes/forever/wowhead-cache/item/<id>.json, outside
git), so a later ticket or the refresh reads it again without a request. The script waits PAUSE seconds between two
requests. An item that Wowhead does not know ("Entity not found", HTTP 404) is printed as `missing`.

Output: one block per item: name, quality, item level, bonding, slot and kind, damage and speed, armor, the base
stats, each equip line, required level, the classes line, sell price in copper, "Dropped by" and "Drop Chance".
Only numbers and names go into the spec.
"""
import argparse
import html
import json
import os
import re
import sys
import time
import urllib.error
import urllib.request

URL = "https://nether.wowhead.com/tooltip/item/%d?dataEnv=16&locale=0"
DEFAULT_CACHE = os.path.expanduser("~/wowai/notes/forever/wowhead-cache")
PAUSE = 2.0
QUALITIES = ("poor", "common", "uncommon", "rare", "epic", "legendary")
# The <!--statN--> numbers of the tooltip (the ItemSparse stat types).
STATS = {"0": "Mana", "1": "Health", "3": "Agi", "4": "Str", "5": "Int", "6": "Spi", "7": "Sta"}


def fetch(item, cache, refetch=False):
    """The raw tooltip JSON of an item ({} for an item that Wowhead does not know), from the cache when it is
    there."""
    path = os.path.join(cache, "item", "%d.json" % item)
    if os.path.isfile(path) and not refetch:
        with open(path, encoding="utf-8") as handle:
            return json.load(handle)
    time.sleep(PAUSE)
    try:
        with urllib.request.urlopen(URL % item, timeout=30) as answer:
            data = json.loads(answer.read().decode("utf-8"))
    except urllib.error.HTTPError as error:
        if error.code != 404:
            raise
        data = {}
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(data, handle)
    return data


def _text(fragment):
    return html.unescape(re.sub(r"<[^>]+>", "", fragment)).strip()


def parse(data):
    """{field: value} of a tooltip answer, or {} when Wowhead does not know the item."""
    tip = data.get("tooltip")
    if not tip:
        return {}
    found = {"name": data.get("name", ""), "quality": QUALITIES[data.get("quality", 1)]}

    def first(pattern, flags=0):
        m = re.search(pattern, tip, flags)
        return m.group(1) if m else None

    found["itemLevel"] = first(r"<!--ilvl-->(\d+)")
    found["requiredLevel"] = first(r"<!--rlvl-->(\d+)")
    found["bonding"] = first(r"<!--bo--><br>([^<]+)")
    slot = re.search(r"<table width=\"100%\"><tr><td>([^<]*)</td><th>(.*?)</th></tr></table>", tip)
    if slot:
        found["slot"], found["kind"] = slot.group(1), _text(slot.group(2))
    found["damage"] = first(r"<!--dmg-->([\d ]+- [\d ]+)Damage")
    found["speed"] = first(r"<!--spd-->([\d.]+)")
    found["armor"] = first(r"<!--amr-->(\d+) Armor")
    found["stats"] = ", ".join("%s %s" % (STATS.get(t, "stat%s" % t), v)
                               for t, v in re.findall(r"<!--stat(\d+)-->([+-]?\d+)", tip))
    found["resistances"] = ", ".join(_text(r) for r in re.findall(r"<span>(\+\d+ \w+ Resistance)</span>", tip))
    found["equip"] = " | ".join(_text(e) for e in re.findall(r"(Equip:.*?)</span>(?:<!--|<br>|$)", tip))
    found["chanceOnHit"] = " | ".join(_text(e) for e in re.findall(r"(Chance on hit:.*?)</span>", tip))
    found["use"] = " | ".join(_text(e) for e in re.findall(r"(Use:.*?)</span>", tip))
    found["classes"] = _text(first(r"(Classes:.*?)<br", re.S) or "")
    found["durability"] = first(r"Durability (\d+) /")
    money = {k: int(first(r'<span class="money%s">(\d+)</span>' % k) or 0) for k in ("gold", "silver", "copper")}
    found["sellPrice"] = str(money["gold"] * 10000 + money["silver"] * 100 + money["copper"]) if "Sell Price" in tip else None
    found["droppedBy"] = first(r"Dropped by: ([^<]+)")
    found["dropChance"] = first(r"Drop Chance: ([\d.]+)%")
    return {k: v for k, v in found.items() if v}


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("items", nargs="+", type=int)
    parser.add_argument("--cache", default=DEFAULT_CACHE)
    parser.add_argument("--refetch", action="store_true", help="ask Wowhead again even when the cache has the item")
    args = parser.parse_args(argv)
    for item in args.items:
        fields = parse(fetch(item, args.cache, args.refetch))
        print("%d\t%s" % (item, "missing" if not fields else URL % item))
        for key, value in fields.items():
            print("  %s: %s" % (key, value))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
