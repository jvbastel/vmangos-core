#!/usr/bin/env python3
"""In-game check of ticket #132 (the healer does not shield a rage tank), on the check kit.

    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/runs.py --ticket N --watch 81NN scenario enforcers 1 &
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t132_no_shield_tank.py

While the scripted run fights its pack, the GM player Thrallson puts the
Warrior tank Grukk near death (20% health) again and again. The Priest Hela
heals it and never shields it: no Power Word: Shield and no Weakened Soul on
the tank. First it puts the mage Ezzra near death once: the Priest shields
the mage, as before.
"""
import re

from kit import Realm, Results, stat

ACCOUNT = "PLAYER132"
PLAYER = "Thrallson"
# Power Word: Shield ranks 1 to 3 (level 16), and Weakened Soul.
SHIELDS = {17, 592, 600}
WEAKENED_SOUL = 6788

realm = Realm()
results = Results()
tank, priest, mage = "Grukk", "Hela", "Ezzra"
tank_guid, mage_guid = realm.guid_of(tank), realm.guid_of(mage)

c = realm.login(ACCOUNT, PLAYER)


def auras(guid):
    """The spell ids of the auras on the unit, from `.list auras`."""
    ids = set()
    for line in c.gm(".list auras", select=guid, wait=0.7):
        m = re.search(r"\bid[: ]+(\d+)", line, re.IGNORECASE)
        if m:
            ids.add(int(m.group(1)))
    return ids


def near_death(guid):
    health, top = stat(c, guid)[:2]
    if top > 0:
        c.gm(".modify hp %d %d" % (max(1, top // 5), top), select=guid, wait=0.5)
    return health, top


# The run fights: its status names the combat wait.
fighting = False
for _ in range(120):
    if "combat_end" in realm.soap(".wowai run status"):
        fighting = True
        break
    c.pump(1)
results.check("the scripted run fights its pack", fighting, realm.soap(".wowai run status"))
c.gm(".gm on")
c.gm(".goname %s" % tank, wait=4.0)
c.pump(2)

realm.mark("mage near death")
near_death(mage_guid)
shielded = False
for _ in range(4):
    c.pump(1)
    if auras(mage_guid) & (SHIELDS | {WEAKENED_SOUL}):
        shielded = True
        break
rules = re.findall(r"\[Bots\] %s: ([a-z ]+)" % priest, realm.log())
results.check("the Priest still shields the mage near death", shielded, repr(rules))

realm.mark("tank near death")
tank_auras = set()
raised = 0
for _ in range(5):
    near_death(tank_guid)
    c.pump(2.0)
    health, top = stat(c, tank_guid)[:2]
    if top > 0 and health > top // 5:
        raised += 1
    tank_auras |= auras(tank_guid)
text = realm.log()
rules = re.findall(r"\[Bots\] %s: ([a-z ]+)" % priest, text)
results.check("the Priest never shields the rage tank", not (tank_auras & (SHIELDS | {WEAKENED_SOUL})),
              "auras %s" % sorted(tank_auras))
results.check("the Priest heals the rage tank near death", raised > 0 and any("heal" in r for r in rules),
              "health raised %d of 5 times, rules %r" % (raised, rules))
results.check("the shield rule never fires while only the tank is near death",
              "shield a member near death" not in rules, repr(rules))

results.finish()
