#!/usr/bin/env python3
"""In-game check of jvbastel/WOWAI#251 (spec #250): a new character takes the hardcore status at the hardcore
NPC in Valley of Trials, and sees it. One run on a slot with the #251 build:

    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t251_hardcore_take.py

The check restarts mangosd once (`restart`), for the status after a server restart. Each run makes new
characters (a name suffix from the time), so it can run again on the same lease.

1. A new level 1 character with 0 XP (the new player) gets the take option. It leads to the rules page and
   the confirmation, and the confirmation sets the status: character_hardcore says Alive, and `.hardcore`
   says "Hardcore: alive".
2. The NPC does not offer the status again to the new hardcore character.
3. The hardcore character sees "Hardcore: alive" at login, after a relog, and after a server restart; the
   status stays (character_hardcore and `.hardcore`).
4. The NPC does not offer the status to a character above level 1 (GM `.levelup`), to a level 1 character
   with XP (xp set by SQL while it is offline), or to a character marked "was hardcore" (status 3 set by SQL
   while it is offline, until #255 adds the drop). None of them sees "Hardcore: alive" at login.
5. `.hardcore` of a character that was never hardcore says so; of the "was hardcore" character, that it
   dropped the status before.
"""
import time

from kit import (HARDCORE_NPC, HARDCORE_TEXT_GREETING, HARDCORE_TEXT_TAKE_RULES, Realm, Results,
                 go_to_hardcore_npc, hardcore_line, take_hardcore)
from wowclient import CHAT_MSG_SYSTEM, GossipClient

ALIVE = "Hardcore: alive"
NEVER = "This character is not hardcore. It was never hardcore."
DROPPED = "This character is not hardcore. It dropped the hardcore status before, and can never take it again."


def suffix():
    """Four lowercase letters from the time, so that each run has new names."""
    n, out = int(time.time()), ""
    for _ in range(4):
        n, k = divmod(n, 26)
        out += chr(ord("a") + k)
    return out


def low(client):
    return client.guid & 0xFFFFFFFF


def status_row(realm, client):
    """The status column of the character's character_hardcore row, or "" without a row."""
    return realm.sql("characters", "SELECT status FROM character_hardcore WHERE guid = %d" % low(client))


def login_lines(client, start):
    return [text for (t, _, text) in client.chat_log[start:] if t == CHAT_MSG_SYSTEM]


def offers_status(client):
    """The NPC's greeting, and whether it offers the take option."""
    go_to_hardcore_npc(client)
    menu = client.gossip_hello(HARDCORE_NPC)
    return menu, (menu is not None and menu.option("hardcore") is not None)


def relog_with(realm, client, query):
    """Logs the character out, runs `query` (with %d for the low guid) on the characters database, and logs
    it in again. The system lines of the new login."""
    client.logout()
    realm.sql("characters", query % low(client))
    start = len(client.chat_log)
    client.login_again()
    return login_lines(client, start)


def close(client):
    try:
        client.logout()
    except Exception:
        pass
    client.sock.close()


realm = Realm()
results = Results()
tag = suffix()
hardcore_name = "Hcnew" + tag

# 1. The new player takes the status, step by step.
player = realm.login("PLAYER251A", hardcore_name, client_class=GossipClient)
results.check("a new character has no hardcore row", status_row(realm, player) == "")
results.check("a new character never hardcore: .hardcore says so", hardcore_line(player) == NEVER, hardcore_line(player))
menu, offered = offers_status(player)
results.check("the NPC answers in Valley of Trials", menu is not None)
results.check("the NPC greets with its greeting text", menu is not None and menu.text_id == HARDCORE_TEXT_GREETING, repr(menu))
results.check("the NPC offers the status to a level 1 character with 0 XP", offered, repr(menu))
rules = player.gossip_select(HARDCORE_NPC, menu.option("hardcore")) if offered else None
results.check("the take option opens the rules page", rules is not None and rules.text_id == HARDCORE_TEXT_TAKE_RULES, repr(rules))
results.check("the rules page asks for a confirmation", rules is not None and rules.option("yes") is not None
              and rules.option("no") is not None, repr(rules))
results.check("the status is not set before the confirmation", status_row(realm, player) == "")
start = len(player.chat_log)
closed = (player.gossip_select(HARDCORE_NPC, rules.option("yes"))
          if rules is not None and rules.option("yes") is not None else "no rules page")
results.check("the confirmation closes the gossip", closed is None)
results.check("the confirmation tells the player", any("hardcore character now" in line for line in login_lines(player, start)),
              login_lines(player, start))
results.check("the status is written at once (character_hardcore status 1)", status_row(realm, player) == "1", status_row(realm, player))
results.check(".hardcore says Hardcore: alive", hardcore_line(player) == ALIVE, hardcore_line(player))

# 2. No second take.
menu, offered = offers_status(player)
results.check("the NPC does not offer the status to a hardcore character", menu is not None and not offered, repr(menu))

# 3. The status at login and after a relog.
start = len(player.chat_log)
player.relog()
results.check("a hardcore character sees Hardcore: alive at login", ALIVE in login_lines(player, start), login_lines(player, start))
results.check("the status stays after a relog", status_row(realm, player) == "1" and hardcore_line(player) == ALIVE)

# 4. The refusals.
leveled = realm.login("PLAYER251B", "Hclvl" + tag, client_class=GossipClient)
leveled.gm(".levelup 1", select=leveled.guid)
menu, offered = offers_status(leveled)
results.check("the NPC does not offer the status above level 1", menu is not None and not offered, repr(menu))
take = take_hardcore(realm, leveled)
results.check("a character above level 1 cannot take the status", not take and status_row(realm, leveled) == "")

with_xp = realm.login("PLAYER251C", "Hcxp" + tag, client_class=GossipClient)
lines = relog_with(realm, with_xp, "UPDATE characters SET xp = 10 WHERE guid = %d")
results.check("a non-hardcore character sees no Hardcore line at login", ALIVE not in lines, lines)
menu, offered = offers_status(with_xp)
results.check("the NPC does not offer the status to a level 1 character with XP", menu is not None and not offered, repr(menu))
results.check("a level 1 character with XP cannot take the status", not take_hardcore(realm, with_xp) and status_row(realm, with_xp) == "")
results.check("never hardcore: .hardcore says so (character with XP)", hardcore_line(with_xp) == NEVER, hardcore_line(with_xp))

dropped = realm.login("PLAYER251D", "Hconce" + tag, client_class=GossipClient)
lines = relog_with(realm, dropped, "INSERT INTO character_hardcore (guid, status, taken_at, dropped_at) "
                   "VALUES (%d, 3, UNIX_TIMESTAMP() - 60, UNIX_TIMESTAMP())")
results.check("a was-hardcore character sees no Hardcore: alive at login", ALIVE not in lines, lines)
menu, offered = offers_status(dropped)
results.check("the NPC does not offer the status to a was-hardcore character", menu is not None and not offered, repr(menu))
results.check("a was-hardcore character cannot take the status", not take_hardcore(realm, dropped) and status_row(realm, dropped) == "3")
results.check(".hardcore of a was-hardcore character says it dropped the status", hardcore_line(dropped) == DROPPED, hardcore_line(dropped))

# 3b. After a server restart.
for client in (player, leveled, with_xp, dropped):
    close(client)
realm.run("restart")
player = realm.login("PLAYER251A", hardcore_name, client_class=GossipClient)
results.check("a hardcore character sees Hardcore: alive at login after a restart", ALIVE in login_lines(player, 0),
              login_lines(player, 0))
results.check("the status stays after a server restart", status_row(realm, player) == "1" and hardcore_line(player) == ALIVE,
              "%s / %s" % (status_row(realm, player), hardcore_line(player)))
menu, offered = offers_status(player)
results.check("after the restart the NPC still does not offer the status again", menu is not None and not offered, repr(menu))
close(player)

results.finish()
