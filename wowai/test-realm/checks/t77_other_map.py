#!/usr/bin/env python3
"""In-game check of ticket #77 (a bot on another map than its leader ignores the group coordinator), on the check kit.
Since #89 such a bot uses the place coordinator of its group and place.

    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t77_other_map.py

The player Mapwalker invites the Warrior tank Grukk, so the player leads the group.

1. The player goes to the Eastern Kingdoms: Grukk, still in Kalimdor, uses the place coordinator.
2. The player summons Grukk: on the leader's map again, Grukk uses the coordinator.
"""
from kit import Realm, Results
from wowclient import CHAT_MSG_PARTY, CHAT_MSG_WHISPER

ACCOUNT = "PLAYER77"
PLAYER = "Mapwalker"
TANK = "Grukk"

realm = Realm()
results = Results()
c = realm.login(ACCOUNT, PLAYER)
realm.mark("start")

c.chat(CHAT_MSG_WHISPER, "invite", target=TANK)
c.pump(4)
c.gm(".revive %s" % TANK)
c.gm(".namego %s" % TANK, wait=2.0)
c.chat(CHAT_MSG_PARTY, "follow")
c.pump(3)
text = realm.log()
results.check("Grukk on the leader's map does not use a place coordinator",
              "%s uses the place coordinator" % TANK not in text, "")

# 1. The leader goes to another map (Brill, Tirisfal Glades).
realm.mark("away")
c.gm(".go xyz 2271 238 34 0", select=c.guid, wait=3.0)
away = realm.wait_log(r"%s uses the place coordinator \(map 1," % TANK, 15, c)
results.check("Grukk on another map than the leader uses the place coordinator", away, realm.log()[-400:])

# 2. Grukk reaches the leader's map.
realm.mark("back")
c.gm(".namego %s" % TANK, wait=3.0)
back = realm.wait_log(r"%s uses the group coordinator \(map 0," % TANK, 15, c)
results.check("Grukk on the leader's map again uses the group coordinator", back, realm.log()[-400:])
results.finish()
