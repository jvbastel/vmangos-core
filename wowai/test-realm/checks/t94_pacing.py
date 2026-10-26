#!/usr/bin/env python3
"""In-game check of ticket #94 (full-run pacing), on the check kit.

    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t94_pacing.py

The scenario `refight` of ragefire_chasm.txt pulls pack1. In the fight the GM
player kills the whole group. Then:
1. the members revive at the dungeon entrance at half health, and each bot
   rests (eats or drinks) until full before the next pull;
2. the pack lives after its fight: the group walks back to the pull point
   and fights it again.
"""
import re

from kit import Realm, Results, stat

ACCOUNT = "PLAYER94"
PLAYER = "Thrallson"

realm = Realm()
results = Results()
run_bots = ["Ezzra", "Grukk", "Hela", "Shanka", "Morzul"]
guids = {name: realm.guid_of(name) for name in run_bots}
bots = [name for name in run_bots if name != "Ezzra"]

c = realm.login(ACCOUNT, PLAYER)
c.gm(".gm on")

realm.mark("refight")
print(" ", realm.soap(".wowai run start ragefire_chasm refight"))
pulled = realm.wait_log(r"Run \d+: line \d+: fight pack1", 120, c)
results.check("the run pulls pack1", pulled, realm.soap(".wowai run status"))
c.gm(".goname Ezzra", wait=3.0)
fighting = realm.wait_log(r"Grukk: (attack|fight|sunder|heroic|revenge|taunt|demoralizing|battle shout)", 20, c)
c.pump(3)

# The Priest last: while it lives, the dead wait for its resurrect.
for name in run_bots:
    if name != "Hela":
        c.gm(".die", select=guids[name], wait=1.0)
c.gm(".die", select=guids["Hela"], wait=1.0)
wiped = realm.wait_log(r"Run \d+: wipe: the test driver releases", 30, c)
results.check("the group wipes in the fight and revives at the entrance", wiped)

# The second fight: the log since the start of the check names it twice.
again = False
for _ in range(200):
    if len(re.findall(r"Run \d+: line \d+: fight pack1", realm.log())) >= 2:
        again = True
        break
    c.pump(1)
text = realm.log()
results.check("the pack lives after its fight: the run fights it again", again, realm.soap(".wowai run status"))
after_wipe = text.split("the test driver releases", 1)[-1]
results.check("the group walks back to the pull point first",
              re.search(r"line \d+: command follow.*line \d+: move pull_pack1.*line \d+: fight pack1", after_wipe, re.S)
              is not None)
before_pull = after_wipe.split("fight pack1", 1)[0]
for name in bots:
    rested = re.search(r"Group \d+ rests: %s (eats|drinks)" % name, before_pull) is not None
    results.check("%s rests after its revive at half health" % name, rested)
if again:
    # Read at once: the tank runs to the pack, and nobody has a hit yet. The
    # others fight by the time a second read comes (one read takes 1 s), and
    # the Priest spends mana on its buffs after its drink. A buff after the
    # meal (Fortitude, +80) raises the max health only.
    health, max_health, _, _ = stat(c, guids["Grukk"])
    results.check("the tank is (nearly) full at the next pull", health >= max_health - 100,
                  "health %d/%d" % (health, max_health))

ended = realm.wait_log(r"Run \d+ ragefire_chasm refight: (passed|failed|aborted)", 300, c)
print(" ", realm.soap(".wowai run status"))
if not ended:
    print(" ", realm.soap(".wowai run stop"))
realm.wait_log(r"Run \d+: the members leave the run group", 30, c)
results.finish()
