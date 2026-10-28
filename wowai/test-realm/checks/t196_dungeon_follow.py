#!/usr/bin/env python3
"""In-game check of ticket #196 (the bots follow the player tank into and out
of a dungeon), on the check kit.

    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t196_dungeon_follow.py

A new player (an Orc Warrior) invites the healer, the melee member and a
ranged member, and no bot tank: the human leader is the tank (bots/GroupTank.h),
so the group is a player-tank group. GM `.go xyz` stands in for the walk
through the instance portal: both move the player to another map.

1. The group stands outside Ragefire Chasm. The tank goes in: each bot appears
   next to the tank inside within a few seconds.
2. The melee member dies inside. The tank leaves: the healer and the ranged
   member appear next to the tank outside; the dead member stays inside.
"""
from kit import Realm, Results, dist, kill, pos
from wowclient import CHAT_MSG_WHISPER

ACCOUNT = "PLAYER196"
PLAYER = "Portalrunner"
HEALER = "Hela"
MELEE = "Shanka"
RANGED = "Morzul"
BOTS = (HEALER, MELEE, RANGED)
RAGEFIRE = 389
KALIMDOR = 1
# Next to the tank: the bots land on its spot, the follow rule spreads them.
NEAR_YARDS = 10.0

realm = Realm()
results = Results()


def trigger(where):
    """(x, y, z, o) of the area trigger destination of `where`."""
    row = realm.sql("mangos", "SELECT target_position_x, target_position_y, target_position_z, target_orientation "
                    "FROM areatrigger_teleport WHERE " + where + " LIMIT 1")
    return tuple(float(v) for v in row.split("\t"))


inside = trigger("target_map = %d" % RAGEFIRE)
outside = trigger("target_map = %d AND name LIKE '%%Ragefire%%'" % KALIMDOR)

c = realm.login(ACCOUNT, PLAYER, race=2, cls=1)
me = realm.guid_of(PLAYER)
c.gm(".levelup 15", select=me)
c.gm(".go xyz %f %f %f %d" % (outside[0], outside[1], outside[2], KALIMDOR), select=me, wait=3.0)
for bot in BOTS:
    c.chat(CHAT_MSG_WHISPER, "invite", target=bot)
    c.pump(4)
    realm.soap(".revive %s" % bot)
    c.gm(".namego %s" % bot, wait=2.0)
c.pump(5)


def near_tank(bot):
    here = pos(c, me)
    there = pos(c, realm.guid_of(bot))
    return here is not None and there is not None and dist(here, there) <= NEAR_YARDS, "%s at %s, tank at %s" % (
        bot, there, here)


# 1. Into Ragefire Chasm.
realm.mark("in")
c.gm(".go xyz %f %f %f %d" % (inside[0], inside[1], inside[2], RAGEFIRE), select=me, wait=3.0)
for bot in BOTS:
    ok = realm.wait_log(r"%s follows the player tank %s into map %d" % (bot, PLAYER, RAGEFIRE), 15, c)
    results.check("%s follows the tank into Ragefire Chasm" % bot, ok, realm.log()[-400:])
c.pump(5)
for bot in BOTS:
    ok, detail = near_tank(bot)
    results.check("%s stands next to the tank inside" % bot, ok, detail)

# 2. The melee member dies inside, then out of Ragefire Chasm.
kill(c, realm.guid_of(MELEE))
c.pump(2)
realm.mark("out")
c.gm(".go xyz %f %f %f %d" % (outside[0], outside[1], outside[2], KALIMDOR), select=me, wait=3.0)
for bot in (HEALER, RANGED):
    ok = realm.wait_log(r"%s follows the player tank %s out to map %d" % (bot, PLAYER, KALIMDOR), 15, c)
    results.check("%s follows the tank out of Ragefire Chasm" % bot, ok, realm.log()[-400:])
c.pump(5)
for bot in (HEALER, RANGED):
    ok, detail = near_tank(bot)
    results.check("%s stands next to the tank outside" % bot, ok, detail)
text = realm.log()
results.check("the dead melee member is not teleported", "%s follows the player tank" % MELEE not in text
              and "%s cannot follow" % MELEE not in text, text[-400:])
results.finish()
