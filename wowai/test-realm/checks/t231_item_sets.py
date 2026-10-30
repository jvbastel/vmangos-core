#!/usr/bin/env python3
"""In-game check of jvbastel/WOWAI#231, the Forever item sets Blessed Plate and Imperial Plate (spec #225), on
the check kit.

    # A slot with the Forever rules:
    wowai/test-realm/realm.sh --ticket N acquire --forever
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t231_item_sets.py forever
    # A slot without them:
    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t231_item_sets.py vanilla

A level 60 Orc Warrior with Plate Mail wears set pieces (GM `.additem`, then CMSG_AUTOEQUIP_ITEM, as a player
equips from the bags) and reads its auras with `.list auras`: a set bonus is the aura of its spell. The
Forever bonuses are the bonus rows of forever/spec/item_sets.tsv (Forever client build 1.60.1.70685).

forever: two stock Imperial Plate pieces give the 2-piece bonus (Defense 7) and not the 3-piece one; Imperial
         Plate Gauntlets (70208, the eighth piece) as the third piece gives the 3-piece bonus (1% hit, melee
         and spell). The stock 2-piece bonus (Armor 100) is gone. Two Blessed Plate pieces give the 2-piece
         bonus only; six give every bonus up to 6.
vanilla: two Imperial Plate pieces give the stock 2-piece bonus (Armor 100) and not Defense 7; four give the
         stock 4-piece bonus (Attack Power 28) and no Forever bonus. The server knows neither Imperial Plate
         Gauntlets nor any Blessed Plate piece (`.additem` gives nothing).

Every stock Imperial Plate piece is patch 2 content, so a server at WowPatch 0 (the slots) refuses it. The
check then says SKIP for the parts that need stock pieces, checks that the refused pieces are patch 2 content,
that Imperial Plate Gauntlets is a piece of set 321 (forever_item_template) and that it alone gives no bonus. Run
the check on a slot acquired with `--set WowPatch=10` to check the stock pieces too.
"""
import re
import struct
import sys
from pathlib import Path

from kit import Realm, Results, clear_backpack
from wowclient import CMSG_DESTROYITEM, SpellClient

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "forever" / "generator"))
from forever_gen import item_sets, spec  # noqa: E402  (the reader of forever/spec/item_sets.tsv)

CMSG_AUTOEQUIP_ITEM = 266
BACKPACK, EQUIPMENT_SLOTS = 255, 19
PLATE_MAIL = 750
LEVEL = 60
BLESSED, IMPERIAL = 1968, 321
IMPERIAL_BELT, IMPERIAL_BRACERS, IMPERIAL_BOOTS, IMPERIAL_CHEST = 12424, 12425, 12426, 12422
IMPERIAL_GAUNTLETS = 70208
BLESSED_PIECES = (70202, 70204, 70206, 70210, 70212, 70214)  # Pauldrons, Boots, Gauntlet, Bracers, Leggings, Belt
# The stock 1.12 bonuses of Imperial Plate (ItemSet.dbc 321): 2 Armor 100, 4 Attack Power 28, 6 Stamina 18.
STOCK_IMPERIAL = {2: [15666], 4: [9335], 6: [14465]}
SKIP_STOCK = ("SKIP: the server refuses the stock Imperial Plate pieces (patch 2 content, above the WowPatch of the "
              "server), so the bonuses of stock pieces are not checked")


def skip_stock(mode):
    """The stock pieces were refused: say SKIP, and check that their patch explains it (a refusal of 1.12
    content of patch 0-1 is a failure, not a skip)."""
    print(SKIP_STOCK)
    patch = realm.sql("mangos", "SELECT MIN(patch) FROM item_template WHERE entry IN (%d, %d)"
                      % (IMPERIAL_BELT, IMPERIAL_BRACERS))
    results.check("%s: the refused stock Imperial Plate pieces are patch 2 content or later" % mode,
                  patch.isdigit() and int(patch) >= 2, "patch %s" % patch)


def forever_bonuses(set_id):
    """{threshold: [spell]} of the ported bonus rows of the set in forever/spec/item_sets.tsv."""
    path = ROOT / "forever" / "spec" / "item_sets.tsv"
    rows = item_sets.read_rows(str(path), spec.data_lines(path.read_text(encoding="utf-8")))["item_sets"]
    found = {}
    for spell, threshold in item_sets.spells(next(s for s in rows if s.set_id == set_id)):
        found.setdefault(threshold, []).append(spell)
    return found


def auras():
    """The spell IDs of the auras on the character (`.list auras`)."""
    found = set()
    for line in c.gm(".list auras", select=me, wait=1.0):
        m = re.search(r"id: (\d+) eff: \d+ type: \d+", line)
        if m:
            found.add(int(m.group(1)))
    return found


def equipped():
    c.gm(".save", select=me)
    rows = realm.sql("characters", "SELECT item_id FROM character_inventory WHERE guid = %d AND bag = 0 AND slot < %d"
                     % (me, EQUIPMENT_SLOTS))
    return {int(row) for row in rows.split() if row.isdigit()}


def backpack_slot_of(entry):
    c.gm(".save", select=me)
    row = realm.sql("characters", "SELECT slot FROM character_inventory WHERE guid = %d AND bag = 0 AND slot >= 23 "
                    "AND item_id = %d ORDER BY slot LIMIT 1" % (me, entry))
    return int(row.split()[0]) if row else None


def wear(*entries):
    """Adds each item and equips it from the backpack. True when the server made each item."""
    made = True
    for entry in entries:
        c.gm(".additem %d" % entry, select=me)
        slot = backpack_slot_of(entry)
        if slot is None:
            made = False
            continue
        c.send(CMSG_AUTOEQUIP_ITEM, struct.pack("<BB", BACKPACK, slot))
        c.pump(1.5)
    return made


def undress():
    """Destroys every equipped item, so that the next set starts from no piece (and the check can run again)."""
    for slot in range(EQUIPMENT_SLOTS):
        c.send(CMSG_DESTROYITEM, struct.pack("<BBBBBB", BACKPACK, slot, 0, 0, 0, 0))
    c.pump(1.5)
    clear_backpack(realm, c)


def check_bonuses(label, bonuses, worn):
    """Each bonus up to `worn` pieces is on the character, each one above is not."""
    have = auras()
    for threshold, spells in sorted(bonuses.items()):
        on = threshold <= worn
        for spell in spells:
            results.check("%s, %d pieces: the %d-piece bonus %d is %s" % (label, worn, threshold, spell,
                                                                         "on" if on else "off"),
                          (spell in have) == on, "auras %s" % sorted(have))


mode = sys.argv[1] if len(sys.argv) > 1 else ""
if mode not in ("forever", "vanilla"):
    sys.exit("usage: t231_item_sets.py forever|vanilla")

realm = Realm()
results = Results()
name = "Setplate" if mode == "forever" else "Setvanilla"
c = realm.login("PLAYER231", name, race=2, cls=1, client_class=SpellClient)
me = c.guid
level = int(realm.sql("characters", "SELECT level FROM characters WHERE guid = %d" % me) or 1)
if level < LEVEL:
    c.gm(".levelup %d" % (LEVEL - level), select=me)
c.gm(".learn %d" % PLATE_MAIL, select=me)
undress()

if mode == "forever":
    imperial = forever_bonuses(IMPERIAL)
    if wear(IMPERIAL_BELT, IMPERIAL_BRACERS):
        results.check("forever: two stock Imperial Plate pieces are worn",
                      {IMPERIAL_BELT, IMPERIAL_BRACERS} <= equipped())
        check_bonuses("forever Imperial Plate", imperial, 2)
        results.check("forever: the stock Imperial 2-piece bonus (Armor 100) is gone", 15666 not in auras())
        made = wear(IMPERIAL_GAUNTLETS)
        results.check("forever: Imperial Plate Gauntlets (70208) is worn", made and IMPERIAL_GAUNTLETS in equipped())
        check_bonuses("forever Imperial Plate with the eighth piece", imperial, 3)
    else:
        skip_stock("forever")
        set_id = realm.sql("mangos", "SELECT set_id FROM forever_item_template WHERE entry = %d" % IMPERIAL_GAUNTLETS)
        results.check("forever: Imperial Plate Gauntlets (70208) is a piece of Imperial Plate (%d)" % IMPERIAL,
                      set_id == str(IMPERIAL), "set_id %s" % set_id)
        made = wear(IMPERIAL_GAUNTLETS)
        results.check("forever: Imperial Plate Gauntlets (70208) is worn", made and IMPERIAL_GAUNTLETS in equipped())
        check_bonuses("forever Imperial Plate Gauntlets alone", imperial, 1)

    undress()
    blessed = forever_bonuses(BLESSED)
    wear(*BLESSED_PIECES[:2])
    results.check("forever: two Blessed Plate pieces are worn", set(BLESSED_PIECES[:2]) <= equipped())
    check_bonuses("forever Blessed Plate", blessed, 2)
    wear(*BLESSED_PIECES[2:])
    results.check("forever: six Blessed Plate pieces are worn", set(BLESSED_PIECES) <= equipped())
    check_bonuses("forever Blessed Plate", blessed, 6)
else:
    if wear(IMPERIAL_BELT, IMPERIAL_BRACERS):
        results.check("vanilla: two Imperial Plate pieces are worn", {IMPERIAL_BELT, IMPERIAL_BRACERS} <= equipped())
        check_bonuses("vanilla Imperial Plate", STOCK_IMPERIAL, 2)
        results.check("vanilla: the Forever 2-piece bonus Defense 7 (13385) is off", 13385 not in auras())
        wear(IMPERIAL_BOOTS, IMPERIAL_CHEST)
        check_bonuses("vanilla Imperial Plate", STOCK_IMPERIAL, 4)
        have = auras()
        forever_spells = {s for spells in forever_bonuses(IMPERIAL).values() for s in spells} - {13385}
        results.check("vanilla: no Forever Imperial bonus is on", not (forever_spells & have),
                      "auras %s" % sorted(have))
    else:
        skip_stock("vanilla")
    for entry in (IMPERIAL_GAUNTLETS,) + BLESSED_PIECES[:1]:
        c.gm(".additem %d" % entry, select=me)
        results.check("vanilla: the server does not know item %d" % entry, backpack_slot_of(entry) is None)
    undress()

results.finish()
