#!/usr/bin/env python3
"""In-game check of jvbastel/WOWAI#256 (spec #250, user stories 34-37): killing the client is no escape from
a fight for a hardcore character. One run on a slot with the #256 build (about 6 minutes):

    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t256_hardcore_disconnect.py

The slot keeps the play realm setting `ForceLogoutDelay = 0` (mangosd.conf.dist.in). Each run makes new
characters (a name suffix from the time), so it can run again on the same lease. A "lost connection" is the
check client closing its socket without a logout request. "In the world" is `characters.online` = 1 (the
logout saves 0) and the watcher still sees the unit.

1. fight: a hardcore character fights a sturdy boar (in combat) and drops its connection. The server logs
   that it keeps the character. It is still in the world 30 and 100 seconds later, and leaves the world
   when the stock delay (120 seconds) ends.
2. death: a hardcore character fights a sturdy boar, drops its connection, and then (a GM sets its health
   to 1) dies to the boar during the delay. It is dead for good: status dead, the lock flag, and the death
   record (the boar's name, zone Durotar, level 1). After the delay it is saved where it died, no ghost.
3. quiet: a hardcore character that is not in combat drops its connection: it leaves the world at once,
   and the server does not keep it.
4. plain: a character that is not hardcore fights a sturdy boar and drops its connection: it leaves the
   world at once, as before.

Not run yet (batch 2 of spec #250 runs it).
"""
import struct
import time

from kit import (Realm, Results, hardcore_locked, hardcore_row, pos, spawn, stat, sturdy, take_hardcore,
                 unit_field)
from wowclient import GossipClient

CMSG_ATTACKSWING = 321
UNIT_FLAG_IN_COMBAT = 0x00080000
PLAYER_FLAGS_GHOST = 0x10
DUROTAR_ZONE = 14
BOAR = 3098  # Mottled Boar
VALLEY = (-620.0, -4250.0, 38.0)  # Valley of Trials, a little west of the hardcore NPC
DELAY = 120  # the stock delay of WorldSession::ForcePlayerLogoutDelay, in seconds
AT_ONCE = 15  # "at once": the socket close, the session update and the save, with room to spare


def suffix():
    """Four lowercase letters from the time, so that each run has new names."""
    n, out = int(time.time()), ""
    for _ in range(4):
        n, k = divmod(n, 26)
        out += chr(ord("a") + k)
    return out


def online(guid):
    return realm.sql("characters", "SELECT online FROM characters WHERE guid = %d" % (guid & 0xFFFFFFFF)) == "1"


def seen(guid):
    """The watcher still sees the unit (its health from `.unit statinfo`)."""
    return stat(watcher, guid)[0] >= 0


def in_combat(guid):
    flags = unit_field(watcher, guid, "UNIT_FIELD_FLAGS")
    return flags >= 0 and bool(flags & UNIT_FLAG_IN_COMBAT)


def wait_offline(guid, seconds):
    """Seconds until `characters.online` is 0, or None after `seconds`."""
    start = time.time()
    while time.time() - start < seconds:
        if not online(guid):
            return time.time() - start
        watcher.pump(1)
    return None


def player(account, name, hardcore):
    client = realm.login(account, name, client_class=GossipClient)
    if hardcore:
        results.check("%s takes the hardcore status" % name, take_hardcore(realm, client))
    client.gm(".gm off")
    client.gm(".go xyz %.1f %.1f %.1f 1" % VALLEY, wait=3.0)
    client.gm(".modify hp 1000000 1000000", select=client.guid)
    return client


def fight(client, label):
    """The character attacks a new sturdy boar (a million health, not stunned: it hits back). True when the
    character is in combat."""
    client.set_active_mover()
    boar = spawn(realm, client, BOAR)
    sturdy(client, boar, freeze=False)
    client.select(boar)
    client.send(CMSG_ATTACKSWING, struct.pack("<Q", boar))
    end = time.time() + 10
    while time.time() < end and not in_combat(client.guid):
        client.pump(1)
    return results.check(label + ": the character is in combat before the connection drops", in_combat(client.guid))


def drop(client):
    """The client dies: its socket closes, with no logout request."""
    try:
        client.sock.close()
    except Exception:
        pass
    return time.time()


realm = Realm()
results = Results()
tag = suffix()
boar_name = realm.sql("mangos", "SELECT name FROM creature_template WHERE entry = %d" % BOAR)

watcher = realm.login("WATCH256", "Dcwatch" + tag)
watcher.gm(".go xyz %.1f %.1f %.1f 1" % VALLEY, wait=3.0)

# 1. A hardcore character in combat stays in the world for the delay.
name = "Dcfight" + tag
hard = player("FIGHT256", name, True)
fight(hard, "fight")
realm.mark("fight")
dropped = drop(hard)
results.check("fight: the server keeps the hardcore character in the world",
              realm.wait_log(r"Hardcore: %s .*lost its connection in combat" % name, 10, watcher))
for at in (30, 100):
    while time.time() - dropped < at:
        watcher.pump(1)
    results.check("fight: still in the world %d seconds after the lost connection" % at,
                  online(hard.guid) and seen(hard.guid))
gone = wait_offline(hard.guid, DELAY + 30 - (time.time() - dropped))
after = time.time() - dropped
results.check("fight: leaves the world when the delay ends (%.0f s)" % after,
              gone is not None and DELAY - 10 <= after <= DELAY + 30)

# 2. A hardcore character in combat drops its connection and dies during the delay.
name = "Dcdeath" + tag
dying = player("DEATH256", name, True)
fight(dying, "death")
realm.mark("death")
dropped = drop(dying)
results.check("death: the server keeps the hardcore character in the world",
              realm.wait_log(r"Hardcore: %s .*lost its connection in combat" % name, 10, watcher))
watcher.gm(".modify hp 1", select=dying.guid)
results.check("death: the boar kills it during the delay",
              realm.wait_log(r"Hardcore: %s .* died for good" % name, 30, watcher))
died = time.time() - dropped
results.check("death: the death comes during the delay (%.0f s after the lost connection)" % died, died < DELAY)
death_spot = pos(watcher, dying.guid)
row = hardcore_row(realm, dying.guid)
print("  death record:", row)
results.check("death: the status is dead", row[:1] == ["2"], row)
results.check("death: the lock flag is in the database", hardcore_locked(realm, dying.guid))
results.check("death: the death record names the boar", len(row) == 5 and row[3] == boar_name, row)
results.check("death: the death record has the zone (Durotar) and the level",
              len(row) == 5 and row[2] == str(DUROTAR_ZONE) and row[4] == "1", row)
gone = wait_offline(dying.guid, DELAY + 30 - (time.time() - dropped))
results.check("death: leaves the world by the end of the delay (%.0f s)" % (time.time() - dropped), gone is not None)
saved = realm.sql("characters", "SELECT player_flags, position_x, position_y FROM characters WHERE guid = %d"
                  % (dying.guid & 0xFFFFFFFF)).split("\t")
results.check("death: saved with no ghost", len(saved) == 3 and not (int(saved[0]) & PLAYER_FLAGS_GHOST), saved)
results.check("death: saved where it died", len(saved) == 3 and death_spot is not None and
              abs(float(saved[1]) - death_spot[0]) < 3 and abs(float(saved[2]) - death_spot[1]) < 3,
              (saved, death_spot))
results.check("death: still dead and locked after the logout",
              hardcore_row(realm, dying.guid)[:1] == ["2"] and hardcore_locked(realm, dying.guid))

# 3. A hardcore character that is not in combat leaves the world at once.
name = "Dcquiet" + tag
quiet = player("QUIET256", name, True)
quiet.pump(6)  # the combat of the hardcore NPC teleport and the GM commands, if any, is long over
results.check("quiet: the character is not in combat", not in_combat(quiet.guid))
realm.mark("quiet")
drop(quiet)
gone = wait_offline(quiet.guid, AT_ONCE)
results.check("quiet: leaves the world at once (%s s)" % (None if gone is None else "%.0f" % gone), gone is not None)
results.check("quiet: the server does not keep it", not realm.wait_log(r"Hardcore: %s .*lost its connection" % name, 1))

# 4. A character that is not hardcore, in combat, leaves the world at once, as before.
name = "Dcplain" + tag
plain = player("PLAIN256", name, False)
results.check("plain: the character is not hardcore", hardcore_row(realm, plain.guid) == [])
fight(plain, "plain")
realm.mark("plain")
drop(plain)
gone = wait_offline(plain.guid, AT_ONCE)
results.check("plain: leaves the world at once (%s s)" % (None if gone is None else "%.0f" % gone), gone is not None)
results.check("plain: the server does not keep it", not realm.wait_log(r"Hardcore: %s .*lost its connection" % name, 1))

drop(watcher)
results.finish()
