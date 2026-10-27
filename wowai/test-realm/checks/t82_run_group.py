#!/usr/bin/env python3
"""In-game check of ticket #82 (the members leave the run group when a scripted run ends), on the check kit.

    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t82_run_group.py

1. pass: the scenario `short` passes. Then the run group is gone, in the game
   and in the database.
2. stop: a run of the scenario `revive` is stopped by a GM (a failed run).
   Then the run group is gone too.
3. invite: the player Nazgrel invites the tank Grukk and the test driver
   Ezzra. Both join the group of the player.
"""
import re

from kit import Realm, Results
from wowclient import CHAT_MSG_WHISPER

ACCOUNT = "PLAYER82"
PLAYER = "Nazgrel"
RUN = ["Ezzra", "Grukk", "Hela", "Shanka", "Morzul"]

realm = Realm()
results = Results()
guids = {name: realm.guid_of(name) for name in RUN}
c = realm.login(ACCOUNT, PLAYER)
c.gm(".gm on")


def run_status_until(pattern, seconds):
    for _ in range(seconds):
        status = realm.soap(".wowai run status")
        if re.search(pattern, status):
            return status
        c.pump(1)
    return realm.soap(".wowai run status")


def group_rows():
    """The group_member rows of the run members, and the groups that the test driver leads."""
    members = realm.sql("characters", "SELECT COUNT(*) FROM group_member WHERE member_guid IN (%s)" %
                        ", ".join(str(g) for g in guids.values()))
    led = realm.sql("characters", "SELECT COUNT(*) FROM `groups` WHERE leader_guid = %d" % guids["Ezzra"])
    return members, led


def check_group_gone(when):
    # The group waits for the chat of the run first: at most 20 s.
    left = realm.wait_log(r"Run \d+: the members leave the run group", 30, c)
    results.check("%s: the log says the members leave the run group" % when, left)
    out = realm.wait_log(r"Grukk is out of its group and logs out", 10, c)
    results.check("%s: in the game, the bots are out of the group (they log out)" % when, out)
    c.pump(3)
    members, led = group_rows()
    results.check("%s: no group_member row of a run member" % when, members == "0", "rows %s" % members)
    results.check("%s: no group of the test driver" % when, led == "0", "groups %s" % led)


print("== pass")
realm.mark("pass")
print(" ", realm.soap(".wowai run start ragefire_chasm short"))
status = run_status_until(r"passed|failed", 120)
results.check("the scenario short passes", "passed" in status, status)
check_group_gone("after a passed run")

print("== stop")
realm.mark("stop")
print(" ", realm.soap(".wowai run start ragefire_chasm revive"))
status = run_status_until(r"wait 120|passed|failed", 120)
results.check("the run reaches its wait", "wait 120" in status, status)
members, _ = group_rows()
results.check("during the run, the five members are in the run group", members == "5", "rows %s" % members)
print(" ", realm.soap(".wowai run stop"))
check_group_gone("after a stopped run")

print("== invite")
realm.mark("invite")
for bot in ("Grukk", "Ezzra"):
    c.chat(CHAT_MSG_WHISPER, "invite", target=bot)
    joined = realm.wait_log(r"%s joins the group of %s" % (bot, PLAYER), 20, c)
    results.check("after the runs, %s joins the group of the player" % bot, joined)
c.pump(3)
player_guid = realm.guid_of(PLAYER)
rows = realm.sql("characters", "SELECT COUNT(*) FROM group_member m JOIN `groups` g ON g.group_id = m.group_id "
                 "WHERE g.leader_guid = %d AND m.member_guid IN (%d, %d)" % (player_guid, guids["Grukk"], guids["Ezzra"]))
results.check("in the database, both are in the group of the player", rows == "2", "rows %s" % rows)
results.finish()
