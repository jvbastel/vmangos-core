#!/usr/bin/env python3
"""In-game check of jvbastel/WOWAI#253 (spec #250): the first death of a hardcore character is final. One run
on a slot with the #253 build (about 5 minutes):

    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t253_hardcore_death.py

The check restarts mangosd once (`restart`). Each run makes new characters (a name suffix from the time), so
it can run again on the same lease. Every character takes the hardcore status at the hardcore NPC of Valley
of Trials (kit.take_hardcore), except the plain character and the watcher.

1. plain: a character that is not hardcore dies (GM `.die`), releases (a ghost) and is revived by a GM
   `.revive`, as before. It gets no hardcore message, row or lock, and stays in the world.
2. monster: a hardcore character at 1 health attacks a boar and dies to it. The death message comes at once;
   the status (dead), the lock flag and the death record (time, zone Durotar, the boar's name, level 1) are
   in the database at once. It chats in its 60 seconds (the watcher hears it). The server logs it out 60
   seconds after the death (SMSG_LOGOUT_COMPLETE). Its next login is refused with the "character locked"
   error (CHAR_LOGIN_LOCKED_FOR_TRANSFER) and the death record (SMSG_NOTIFICATION).
3. fall: a hardcore character falls 80 yards (client movement packets) and dies: final, cause "a fall". It
   logs out at once and logs in again in its 60 seconds: refused, with the death record.
4. gm die: a hardcore character with a Soulstone dies to a GM `.die` next to a spirit healer: final. Its
   self-resurrect spell is 0 after the death (no Soulstone button). A release request, a GM `.revive`, a
   Resurrection spell (accepted), the Soulstone (CMSG_SELF_RES) and the spirit healer all leave it dead.
5. relog: a hardcore character dies, its client drops the connection, and a new connection logs in: refused.
6. The plain character of part 1 is still in the world after all that (no 60-second logout for it).
7. restart: after a server restart the logins of the dead characters are still refused.
8. delete: the dead character of part 2 is deleted from the character list. Its rows go. A new character with
   the same name is made and takes the hardcore status.

Not run yet (batch 2 of spec #250 runs it). Parts that a tester may need to adjust: the fall (the client
movement packets: a heartbeat with MOVEFLAG_FALLINGFAR 80 yards up, then MSG_MOVE_FALL_LAND with a 4 s fall
time; the movement anticheat may want a jump), and the spirit healer (CanInteractWithNPC for an unreleased
body).
"""
import hashlib
import struct
import time

import wowclient
from kit import (Realm, Results, pos, spawn, stat, take_hardcore)
from wowclient import CHAT_MSG_SAY, CHAT_MSG_SYSTEM, GossipClient

CMSG_CHAR_DELETE = 56
SMSG_CHAR_DELETE = 60
SMSG_CHARACTER_LOGIN_FAILED = 65
CMSG_ATTACKSWING = 321
CMSG_REPOP_REQUEST = 346
SMSG_RESURRECT_REQUEST = 347
CMSG_RESURRECT_RESPONSE = 348
SMSG_NOTIFICATION = 459
CMSG_SPIRIT_HEALER_ACTIVATE = 540
CMSG_SELF_RES = 691
MSG_MOVE_FALL_LAND = 201
MOVEFLAG_FALLINGFAR = 0x4000

CHAR_LOGIN_LOCKED_FOR_TRANSFER = 0x44  # ResponseCodes in SharedDefines.h (build 5875)
CHAR_DELETE_SUCCESS = 0x39
CHARACTER_FLAG_LOCKED_FOR_TRANSFER = 0x4
PLAYER_FLAGS_GHOST = 0x10
DUROTAR_ZONE = 14  # Durotar (AreaTable.dbc); Valley of Trials is an area of it
BOAR = 3098  # Mottled Boar
SPIRIT_HEALER = 6491
RESURRECTION = 2006  # Priest Resurrection, rank 1
SOULSTONE = 20707  # Soulstone Resurrection, rank 1 (the aura of a used Soulstone)
VALLEY = (-606.0, -4248.0, 39.0)  # next to the hardcore NPC

DEAD_FOREVER = "dead forever"
RECORD = "died for good"


class Client(GossipClient):
    """A GossipClient that also keeps the notifications, the login failures, the resurrect requests, the
    character delete answers and the logout completes."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.notes = []
        self.login_failed = []
        self.resurrect_requests = []
        self.deleted = []
        self.logged_out_at = None

    def _observe(self, opcode, body):
        super()._observe(opcode, body)
        if opcode == SMSG_NOTIFICATION:
            self.notes.append(wowclient.cstr(body, 0)[0])
        elif opcode == SMSG_CHARACTER_LOGIN_FAILED:
            self.login_failed.append(body[0])
        elif opcode == SMSG_RESURRECT_REQUEST:
            self.resurrect_requests.append(struct.unpack_from("<Q", body, 0)[0])
        elif opcode == SMSG_CHAR_DELETE:
            self.deleted.append(body[0])
        elif opcode == wowclient.SMSG_LOGOUT_COMPLETE:
            self.logged_out_at = time.time()


def suffix():
    """Four lowercase letters from the time, so that each run has new names."""
    n, out = int(time.time()), ""
    for _ in range(4):
        n, k = divmod(n, 26)
        out += chr(ord("a") + k)
    return out


def low(client):
    return client.guid & 0xFFFFFFFF


def system_lines(client, start):
    return [text for (t, _, text) in client.chat_log[start:] if t == CHAT_MSG_SYSTEM]


def login(account, name, cls=1):
    return realm.login(account, name, cls=cls, client_class=Client)


def connect(account):
    """A new connection of the account, at the character list (no character logged in)."""
    key = realm.run("key", account).strip()
    client = Client(realm.host, realm.world_port, account, wowclient.key_bytes(key))
    body = client.wait_for(wowclient.SMSG_AUTH_CHALLENGE)
    server_seed = struct.unpack("<I", body[:4])[0]
    client_seed = 12345
    digest = hashlib.sha1(client.account.encode() + struct.pack("<III", 0, client_seed, server_seed) + client.key).digest()
    client.send(wowclient.CMSG_AUTH_SESSION, struct.pack("<II", 5875, 0) + client.account.encode() + b"\0" +
                struct.pack("<I", client_seed) + digest + struct.pack("<I", 0))
    client.crypt = wowclient.Crypt(client.key)
    answer = client.wait_for(wowclient.SMSG_AUTH_RESPONSE)
    if answer[0] != 0x0C:
        raise RuntimeError("auth response %d" % answer[0])
    return client


def try_login(client, guid):
    """Sends CMSG_PLAYER_LOGIN for `guid` from the character list. ("refused", code, notes) when the server
    answers SMSG_CHARACTER_LOGIN_FAILED, ("in world", None, notes) when the character enters the world,
    ("nothing", None, notes) when no answer comes in 15 seconds or the connection closes."""
    client.login_failed, client.notes = [], []
    client.send(wowclient.CMSG_PLAYER_LOGIN, struct.pack("<Q", guid))
    end = time.time() + 15
    try:
        while time.time() < end:
            op, _ = client.recv(max(0.1, end - time.time()))
            if op == SMSG_CHARACTER_LOGIN_FAILED:
                client.pump(1.5)  # the death record follows
                return "refused", client.login_failed[0], list(client.notes)
            if op == wowclient.SMSG_LOGIN_VERIFY_WORLD:
                return "in world", None, list(client.notes)
    except Exception as error:  # timeout or a closed connection
        print("  login answer:", error)
    return "nothing", None, list(client.notes)


def hardcore_row(client_or_guid):
    guid = client_or_guid if isinstance(client_or_guid, int) else low(client_or_guid)
    row = realm.sql("characters", "SELECT status, death_time, death_zone, death_killer, death_level "
                    "FROM character_hardcore WHERE guid = %d" % guid)
    return row.split("\t") if row else []


def locked(client):
    flags = realm.sql("characters", "SELECT character_flags FROM characters WHERE guid = %d" % low(client))
    return bool(flags) and bool(int(flags) & CHARACTER_FLAG_LOCKED_FOR_TRANSFER)


def health(watcher, guid):
    return stat(watcher, guid)[0]


def field(viewer, guid, name):
    """An update field of a unit (GM `.debug getvaluebyname`), or -1."""
    for line in viewer.gm(".debug getvaluebyname %s" % name, select=guid):
        if line.startswith(name + ":"):
            try:
                return int(line.split(":", 1)[1].split("/")[0])
            except ValueError:
                return -1
    return -1


def new_hardcore(account, name, cls=1):
    client = login(account, name, cls=cls)
    took = take_hardcore(realm, client)
    results.check("%s takes the hardcore status" % name, took)
    client.gm(".gm off")
    return client


def death_checks(label, client, start, killer=None):
    """The death message, the status, the lock and the death record, read at once after the death."""
    lines = system_lines(client, start)
    results.check(label + ": the death message comes at once", any(DEAD_FOREVER in line for line in lines), lines)
    results.check(label + ": the death message also shows on the screen", any(DEAD_FOREVER in n for n in client.notes),
                  client.notes)
    row = hardcore_row(client)
    print("  death record:", row)
    results.check(label + ": the status is dead in the database at once", row[:1] == ["2"], row)
    results.check(label + ": the lock flag is in the database at once", locked(client))
    results.check(label + ": the death record has the time", len(row) == 5 and abs(int(row[1]) - time.time()) < 120, row)
    results.check(label + ": the death record has the level", len(row) == 5 and row[4] == "1", row)
    if killer is not None:
        results.check(label + ": the death record names the killer or cause (%s)" % killer,
                      len(row) == 5 and row[3] == killer, row)
    else:
        results.check(label + ": the death record has a killer or cause", len(row) == 5 and row[3] != "", row)
    return row


def close(client):
    try:
        client.sock.close()
    except Exception:
        pass


realm = Realm()
results = Results()
tag = suffix()
boar_name = realm.sql("mangos", "SELECT name FROM creature_template WHERE entry = %d" % BOAR)

watcher = login("WATCH253", "Hcwatch" + tag)
watcher.gm(".gm off")

# 1. A plain character: dies, releases, is revived, as before.
plain = login("PLAIN253", "Plain" + tag)
plain.gm(".gm off")
plain.gm(".go xyz %.1f %.1f %.1f 1" % VALLEY, wait=3.0)
start = len(plain.chat_log)
plain.notes = []
plain.gm(".die", select=plain.guid, wait=2.0)
results.check("plain: the character dies", health(plain, plain.guid) == 0)
results.check("plain: no hardcore message", not any(DEAD_FOREVER in line for line in system_lines(plain, start) + plain.notes))
results.check("plain: no hardcore row and no lock", hardcore_row(plain) == [] and not locked(plain))
plain.send(CMSG_REPOP_REQUEST)
plain.pump(3)
ghost = field(plain, plain.guid, "PLAYER_FLAGS")
results.check("plain: the release makes a ghost", ghost >= 0 and bool(ghost & PLAYER_FLAGS_GHOST), ghost)
plain.gm(".revive", select=plain.guid, wait=2.0)
results.check("plain: a GM .revive brings it back", health(plain, plain.guid) > 0)

# 2. A hardcore character dies to a monster.
mob = new_hardcore("MOB253", "Hcmob" + tag)
mob.gm(".go xyz %.1f %.1f %.1f 1" % VALLEY, wait=3.0)
watcher.gm(".go xyz %.1f %.1f %.1f 1" % VALLEY, wait=3.0)
mob.set_active_mover()
boar = spawn(realm, mob, BOAR)
mob.gm(".modify hp 1", select=mob.guid)
realm.mark("monster")
start, mob.notes = len(mob.chat_log), []
mob.select(boar)
mob.send(CMSG_ATTACKSWING, struct.pack("<Q", boar))
end = time.time() + 40
while time.time() < end and not any(DEAD_FOREVER in line for line in system_lines(mob, start)):
    mob.pump(0.5)
died_at = time.time()
row = death_checks("monster", mob, start, killer=boar_name)
results.check("monster: the death record has the zone (Durotar)", len(row) == 5 and row[2] == str(DUROTAR_ZONE), row)
results.check("monster: the server logs the death", realm.wait_log(r"Hardcore: Hcmob%s .* died for good" % tag, 5, mob))
results.check("monster: the character is dead", health(watcher, mob.guid) == 0)
death_spot = pos(watcher, mob.guid)
# Chat in the 60 seconds: the watcher hears the dead character say a line.
said = "farewell " + tag
heard_from = len(watcher.chat_log)
mob.chat(CHAT_MSG_SAY, said)
watcher.pump(2)
results.check("monster: the dead character can chat in its 60 seconds",
              any(text == said and sender == mob.guid for (_, sender, text) in watcher.chat_log[heard_from:]))
# The logout 60 seconds after the death.
end = died_at + 90
while time.time() < end and mob.logged_out_at is None:
    try:
        mob.pump(1)
    except ConnectionError:
        break
after = (mob.logged_out_at or time.time()) - died_at
results.check("monster: the server logs the character out about 60 seconds after the death (%.0f s)" % after,
              mob.logged_out_at is not None and 55 <= after <= 75)
# The logout saves the body where it died: no ghost, no corpse, no teleport to the graveyard.
saved = realm.sql("characters", "SELECT player_flags, position_x, position_y FROM characters WHERE guid = %d"
                  % low(mob)).split("\t")
results.check("monster: after the logout the saved character is no ghost",
              len(saved) == 3 and not (int(saved[0]) & PLAYER_FLAGS_GHOST), saved)
results.check("monster: after the logout the character is saved where it died", len(saved) == 3 and
              death_spot is not None and abs(float(saved[1]) - death_spot[0]) < 2 and
              abs(float(saved[2]) - death_spot[1]) < 2, (saved, death_spot))
results.check("monster: after the logout there is no corpse",
              realm.sql("characters", "SELECT COUNT(*) FROM corpse WHERE player_guid = %d" % low(mob)).strip() == "0")
outcome, code, notes = try_login(mob, mob.guid)
results.check("monster: the next login is refused with the character locked error",
              outcome == "refused" and code == CHAR_LOGIN_LOCKED_FOR_TRANSFER, (outcome, code))
results.check("monster: the refusal shows the death record", any(RECORD in n and boar_name in n and "Durotar" in n for n in notes),
              notes)

# 3. A hardcore character dies from a fall, and logs in again in its 60 seconds.
fall = new_hardcore("FALL253", "Hcfall" + tag)
fall.gm(".go xyz %.1f %.1f %.1f 1" % VALLEY, wait=3.0)
ground = pos(fall, fall.guid)
fall.set_active_mover()
fall.gm(".go xyz %.1f %.1f %.1f 1" % (ground[0], ground[1], ground[2] + 80), wait=3.0)
start, fall.notes = len(fall.chat_log), []
fall.move(wowclient.MSG_MOVE_HEARTBEAT, ground[0], ground[1], ground[2] + 80, 0.0, flags=MOVEFLAG_FALLINGFAR)
fall.pump(0.5)
body = struct.pack("<II4fI", 0, int(time.time() * 1000) & 0xFFFFFFFF, ground[0], ground[1], ground[2], 0.0, 4000)
fall.send(MSG_MOVE_FALL_LAND, body)
fall.pump(3)
died_at = time.time()
death_checks("fall", fall, start, killer="a fall")
fall.send(wowclient.CMSG_LOGOUT_REQUEST)
try:
    fall.wait_for(wowclient.SMSG_LOGOUT_COMPLETE, 30)
    fall.pump(1)
except Exception as error:
    print("  fall logout:", error)
outcome, code, notes = try_login(fall, fall.guid)
results.check("fall: a relog in the 60 seconds is refused with the character locked error (%.0f s after the death)"
              % (time.time() - died_at), outcome == "refused" and code == CHAR_LOGIN_LOCKED_FOR_TRANSFER and
              time.time() - died_at < 60, (outcome, code))
results.check("fall: the refusal shows the death record", any(RECORD in n and "a fall" in n for n in notes), notes)

# 4. A GM .die next to a spirit healer, with a Soulstone; then every way back is tried.
healer_row = realm.sql("mangos", "SELECT guid, position_x, position_y, position_z FROM creature WHERE id = %d AND map = 1 "
                       "ORDER BY POW(position_x - (%f), 2) + POW(position_y - (%f), 2) LIMIT 1"
                       % (SPIRIT_HEALER, VALLEY[0], VALLEY[1])).split("\t")
healer = (0xF130 << 48) | (SPIRIT_HEALER << 24) | int(healer_row[0])
near_healer = (float(healer_row[1]) - 2.0, float(healer_row[2]), float(healer_row[3]))
die = new_hardcore("DIE253", "Hcdie" + tag)
die.gm(".go xyz %.1f %.1f %.1f 1" % near_healer, wait=3.0)
watcher.gm(".go xyz %.1f %.1f %.1f 1" % near_healer, wait=3.0)
die.gm(".aura %d" % SOULSTONE, select=die.guid)
self_res = field(die, die.guid, "PLAYER_SELF_RES_SPELL")
results.check("gm die: with a Soulstone the character has a self-resurrect spell before the death", self_res > 0, self_res)
realm.mark("gm die")
start, die.notes = len(die.chat_log), []
die.gm(".die", select=die.guid, wait=2.0)
died_at = time.time()
death_checks("gm die", die, start)
self_res = field(watcher, die.guid, "PLAYER_SELF_RES_SPELL")
results.check("gm die: no self-resurrect spell after the death (no Soulstone button)", self_res == 0, self_res)
where = pos(watcher, die.guid)

start = len(die.chat_log)
die.send(CMSG_REPOP_REQUEST)
die.pump(3)
results.check("gm die: the release request is refused with a reason",
              any("cannot release" in line for line in system_lines(die, start)), system_lines(die, start))
results.check("gm die: no ghost after the release request", not (field(watcher, die.guid, "PLAYER_FLAGS") & PLAYER_FLAGS_GHOST))
now = pos(watcher, die.guid)
results.check("gm die: the character stays where it died", where is not None and now is not None and
              abs(where[0] - now[0]) < 1 and abs(where[1] - now[1]) < 1, (where, now))


def refused_resurrect(label):
    ok = realm.wait_log(r"Hardcore: refused the resurrect of Hcdie%s" % tag, 5, die)
    results.check("gm die: %s reaches the resurrect and is refused" % label, ok)
    results.check("gm die: %s leaves the character dead" % label, health(watcher, die.guid) == 0)


realm.mark("revive")
print("  revive:", realm.soap(".revive Hcdie%s" % tag))
refused_resurrect("a GM .revive")

realm.mark("resurrection")
die.resurrect_requests = []
watcher.gm(".cast %d triggered" % RESURRECTION, select=die.guid, wait=2.0)
die.pump(2)
results.check("gm die: the Resurrection spell asks the dead character", watcher.guid in die.resurrect_requests,
              die.resurrect_requests)
die.send(CMSG_RESURRECT_RESPONSE, struct.pack("<QB", watcher.guid, 1))
die.pump(2)
refused_resurrect("an accepted Resurrection spell")

die.send(CMSG_SELF_RES)
die.pump(2)
results.check("gm die: the Soulstone (CMSG_SELF_RES) leaves the character dead", health(watcher, die.guid) == 0)

realm.mark("spirit healer")
die.send(CMSG_SPIRIT_HEALER_ACTIVATE, struct.pack("<Q", healer))
die.pump(2)
refused_resurrect("the spirit healer")
print("  the resurrect tries took %.0f s of the 60" % (time.time() - died_at))

# 5. A dropped connection and a new login.
drop = new_hardcore("DROP253", "Hcdrop" + tag)
drop.gm(".die", select=drop.guid, wait=2.0)
results.check("relog: the character died for good", hardcore_row(drop)[:1] == ["2"])
close(drop)
again = connect("DROP253")
outcome, code, notes = try_login(again, drop.guid)
results.check("relog: a login after a dropped connection is refused with the character locked error",
              outcome == "refused" and code == CHAR_LOGIN_LOCKED_FOR_TRANSFER, (outcome, code))
results.check("relog: the refusal shows the death record", any(RECORD in n for n in notes), notes)
close(again)

# 6. The plain character, logged in since part 1, is still in the world: no 60-second logout for it.
mob_guid, mob_name = mob.guid, "Hcmob" + tag
for client in (watcher, mob, fall, die):
    close(client)
results.check("plain: the plain character is still in the world after all that",
              len(plain.gm(".gps", select=plain.guid)) > 0)
results.check("plain: still alive", health(plain, plain.guid) > 0)
close(plain)

# 7. After a server restart.
realm.run("restart")
for account, guid, name in (("MOB253", mob_guid, mob_name), ("DIE253", die.guid, "Hcdie" + tag)):
    client = connect(account)
    outcome, code, notes = try_login(client, guid)
    results.check("restart: the login of %s is still refused with the character locked error" % name,
                  outcome == "refused" and code == CHAR_LOGIN_LOCKED_FOR_TRANSFER, (outcome, code))
    results.check("restart: the refusal of %s still shows the death record" % name, any(RECORD in n for n in notes), notes)
    if account != "MOB253":
        close(client)
        continue

    # 8. Delete the dead character, and make a new one with the same name.
    listed = client._find_char(name)[0]
    results.check("delete: the dead character is still in the character list", listed == guid)
    client.send(CMSG_CHAR_DELETE, struct.pack("<Q", guid))
    client.pump(3)
    results.check("delete: the dead character can be deleted", client.deleted[-1:] == [CHAR_DELETE_SUCCESS], client.deleted)
    results.check("delete: its characters row goes", realm.guid_of(name) == 0)
    results.check("delete: its character_hardcore row goes", hardcore_row(guid & 0xFFFFFFFF) == [])
    close(client)

reborn = login("MOB253", mob_name)
results.check("delete: a new character with the same name is made", reborn.guid not in (0, mob_guid), hex(reborn.guid))
results.check("delete: the new character takes the hardcore status", take_hardcore(realm, reborn))
close(reborn)

results.finish()
