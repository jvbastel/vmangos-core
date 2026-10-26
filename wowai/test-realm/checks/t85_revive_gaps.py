#!/usr/bin/env python3
"""In-game check of ticket #85 (revive rule gaps), on the check kit.

    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t85_revive_gaps.py [part]

The player Thrallson (a GM) invites the Warrior tank Grukk, the Priest healer
Hela, and the Mage Ezzra. `part`: run one part only (noroute, away).

1. noroute: in Wailing Caverns (map 43, no ghost route) the Priest and the
   mage die and release. Each ghost logs one line that names map 43 (Wailing Caverns), once.
2. away (a scripted run): Shanka logs out, then the other members die. The
   members in the world revive at the dungeon entrance without Shanka.
"""
import math
import re
import sys

from kit import Realm, Results, pos, stat
from wowclient import CHAT_MSG_PARTY, CHAT_MSG_WHISPER

ACCOUNT = "PLAYER85"
PLAYER = "Thrallson"
WC_INSIDE = (-158.441, 131.601, -74.2552)  # areatrigger_teleport 228
RFC_ENTRANCE = (0.80, -8.23, -15.53)  # areatrigger_teleport 2230

realm = Realm()
results = Results()
tank, priest, mage = "Grukk", "Hela", "Ezzra"
bots = {name: realm.guid_of(name) for name in (tank, priest, mage)}
only = sys.argv[1] if len(sys.argv) > 1 else ""

c = realm.login(ACCOUNT, PLAYER)


def dist(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def kill(guid):
    return c.gm(".die", select=guid, wait=1.0)


def revive_all():
    for bot in bots:
        realm.soap(".revive %s" % bot)
    c.gm(".revive", select=c.guid)


def gather():
    for bot in bots:
        c.gm(".namego %s" % bot, wait=2.0)
    c.chat(CHAT_MSG_PARTY, "follow")
    c.pump(6)


def part_noroute():
    c.gm(".go xyz %f %f %f 43" % WC_INSIDE, select=c.guid, wait=4.0)
    c.pump(3)
    gather()
    c.chat(CHAT_MSG_PARTY, "stay")
    c.pump(2)
    realm.mark("noroute")
    kill(bots[priest])
    kill(bots[mage])
    released = all(realm.wait_log(r"%s: release when no resurrect comes" % bot, 15, c) for bot in (priest, mage))
    results.check("the Priest and the mage release", released)
    logged = all(realm.wait_log(r"\[Bots\] %s is a ghost of map 43 \(Wailing Caverns\), which has no ghost route" % bot, 20, c)
                 for bot in (priest, mage))
    # The ghosts think a few more times: the line stays one line.
    c.pump(10)
    text = realm.log()
    lines = re.findall(r"\[Bots\] \w+ is a ghost of map \d+ \([^)]*\), which has no ghost route.*", text)
    print("  lines:", lines)
    results.check("each ghost with no ghost route logs a line that names map 43 (Wailing Caverns)", logged)
    results.check("each ghost logs it once", all(sum(bot in line for line in lines) == 1 for bot in (priest, mage)),
                  repr(lines))
    revive_all()
    c.gm(".go xyz -500 -4400 30 1", select=c.guid, wait=4.0)


def part_away():
    realm.mark("away")
    print(" ", realm.soap(".wowai run start ragefire_chasm revive"))
    started = False
    for _ in range(90):
        if "wait 120" in realm.soap(".wowai run status"):
            started = True
            break
        c.pump(1)
    results.check("the run reaches its wait", started, realm.soap(".wowai run status"))
    run_bots = ["Ezzra", "Grukk", "Hela", "Morzul"]
    guids = {name: realm.guid_of(name) for name in run_bots}
    print(" ", realm.soap(".wowai bot logout Shanka"))
    out = False
    for _ in range(20):
        if realm.sql("characters", "SELECT online FROM characters WHERE name = 'Shanka'").split()[-1] == "0":
            out = True
            break
        c.pump(1)
    results.check("Shanka logged out", out)
    c.gm(".goname Ezzra", wait=4.0)
    c.pump(3)
    realm.mark("away wipe")
    # The Priest last: while it lives, the dead wait for its resurrect.
    for name in run_bots:
        if name != "Hela":
            kill(guids[name])
    kill(guids["Hela"])
    wiped = realm.wait_log(r"Run \d+: wipe: everyone revives at the dungeon entrance \(1 not in the world\)", 20, c)
    results.check("with a member logged out, the wipe still revives the others", wiped, realm.log()[-600:])
    c.gm(".go xyz %f %f %f 389" % RFC_ENTRANCE, select=c.guid, wait=3.0)
    c.pump(4)
    for name in run_bots:
        at = pos(c, guids[name])
        health = stat(c, guids[name])[0]
        results.check("%s is alive at the entrance" % name, at is not None and dist(at, RFC_ENTRANCE) < 15 and health > 0,
                      "at %r health %d" % (at, health))
    print(" ", realm.soap(".wowai run stop"))
    realm.wait_log(r"Run \d+: the members leave the run group", 30, c)
    print(" ", realm.soap(".wowai bot login Shanka"))
    c.gm(".go xyz -500 -4400 30 1", select=c.guid, wait=4.0)


c.gm(".gm on")
parts = {"noroute": part_noroute, "away": part_away}
if not only or only == "noroute":
    for bot in bots:
        c.chat(CHAT_MSG_WHISPER, "invite", target=bot)
        c.pump(4)
    revive_all()
for name, part in parts.items():
    if only and name != only:
        continue
    print("== %s" % name)
    part()

results.finish()
