#!/usr/bin/env python3
"""In-game check of jvbastel/WOWAI#255 (spec #250): the hardcore NPC in all 6 starting areas and all 6
capitals, and dropping the hardcore status. One run on a slot with the #251-#255 build:

    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t255_hardcore_drop.py [part]

`part`: run one part only (spawns, drop, after, group, dead). Each run makes new characters (a name suffix
from the time), so it can run again on the same lease. All characters are Horde (orc warriors); a character
tours the Alliance capitals in GM mode (`.gm on`), so that no guard kills it.

1. spawns: the world has the 12 spawns of creature 190250 (11 of them from #255). A level 1 character with
   0 XP that was never hardcore gets the take option at each starting area NPC, and nothing at a capital NPC
   (the capital greeting with no option). A hardcore character gets only the drop option at each capital
   NPC.
2. drop: a hardcore character drops the status at the Valley of Trials NPC: the drop option opens the
   warning page (a dropped status never comes back) with a confirmation; the status stays until the
   confirmation; after it, character_hardcore says Dropped with dropped_at, `.hardcore` says that it was
   hardcore before, and the NPC offers neither the status nor the drop, also after a relog.
3. after: the dropped character and a character that was never hardcore trade, mail each other, and group
   (both directions), and the dropped character opens the auction house. Needs #254's packets.
4. group: two hardcore characters in one group (#254 allows it); one drops the status at the Orgrimmar NPC,
   and both are Dropped at once, the other told so in its chat.
5. dead: a hardcore character dies next to the Valley of Trials NPC (#253 makes the death final): the NPC
   does not offer it the drop, and the character stays dead (status 2).
"""
import sys
import time

from kit import (HARDCORE_NPC, HARDCORE_NPC_ENTRY, HARDCORE_SPAWNS, HARDCORE_TEXT_CAPITAL_GREETING,
                 HARDCORE_TEXT_DROP_WARNING, HARDCORE_TEXT_GREETING, Realm, Results, drop_hardcore,
                 go_to_hardcore_npc, hardcore_line, hardcore_spawn, kill, take_hardcore)
from wowclient import CHAT_MSG_SYSTEM, GossipClient
# The trade, mail, auction house and group invite packets of #254. This check expects these names (the
# merger of #254 and #255 aligns them with what #254 wrote):
#   TradeClient.trade_open(guid) -> True when the trade window opens (SMSG_TRADE_STATUS open window);
#       TradeClient.trade_cancel()
#   MailClient.send_mail(mailbox, to_name, subject, body) -> the mail result (0 = MAIL_OK)
#   AuctionClient.auction_hello(auctioneer) -> True when the auction house opens (MSG_AUCTION_HELLO)
#   GroupClient.group_invite(name) -> the party result (0 = ERR_PARTY_RESULT_OK); group_accept();
#       group_leave()
from wowclient import AuctionClient, GroupClient, MailClient, TradeClient

DROPPED = "This character is not hardcore. It dropped the hardcore status before, and can never take it again."
ALIVE = "Hardcore: alive"
ORGRIMMAR_MAILBOX = 10107  # gameobject guid: the mailbox by the Orgrimmar bank
ORGRIMMAR_AUCTIONEER = 8673  # Auctioneer Thathung, Orgrimmar auction house (one spawn per patch range)


class Client(GossipClient, TradeClient, MailClient, AuctionClient, GroupClient):
    pass


def suffix():
    """Four lowercase letters from the time, so that each run has new names."""
    n, out = int(time.time()), ""
    for _ in range(4):
        n, k = divmod(n, 26)
        out += chr(ord("a") + k)
    return out


def low(client):
    return client.guid & 0xFFFFFFFF


def name_of(client):
    """The character name of a client."""
    return client.names[client.guid]


def status_row(realm, client):
    """The status column of the character's character_hardcore row, or "" without a row."""
    return realm.sql("characters", "SELECT status FROM character_hardcore WHERE guid = %d" % low(client))


def system_lines(client, start):
    return [text for (t, _, text) in client.chat_log[start:] if t == CHAT_MSG_SYSTEM]


def greeting(client, spawn=None):
    """The greeting menu of the hardcore NPC of `spawn` (default Valley of Trials), and the NPC's guid."""
    npc = go_to_hardcore_npc(client, spawn)
    return client.gossip_hello(npc), npc


def close(client):
    try:
        client.logout()
    except Exception:
        pass
    client.sock.close()


def login(name, account):
    return realm.login(account, name, client_class=Client)


def part_spawns():
    rows = realm.sql("mangos", "SELECT guid FROM creature WHERE id = %d ORDER BY guid" % HARDCORE_NPC_ENTRY).split()
    expected = [str(spawn.low) for spawn in HARDCORE_SPAWNS]
    results.check("the world has the 12 spawns of the hardcore NPC", rows == expected, rows)

    new = login("Hcnone" + tag, "PLAYER255A")
    new.gm(".gm on")
    for spawn in HARDCORE_SPAWNS:
        menu, _ = greeting(new, spawn)
        if spawn.capital:
            results.check("%s: the NPC offers nothing to a character that is not hardcore" % spawn.place,
                          menu is not None and menu.text_id == HARDCORE_TEXT_CAPITAL_GREETING and not menu.options,
                          repr(menu))
        else:
            results.check("%s: the NPC offers the status to a new character" % spawn.place,
                          menu is not None and menu.text_id == HARDCORE_TEXT_GREETING
                          and menu.option("hardcore") is not None and menu.option("drop") is None, repr(menu))
    close(new)

    hardcore = login("Hctour" + tag, "PLAYER255B")
    results.check("a new character takes the status (tour)", take_hardcore(realm, hardcore))
    hardcore.gm(".gm on")
    for spawn in HARDCORE_SPAWNS:
        menu, _ = greeting(hardcore, spawn)
        if spawn.capital:
            results.check("%s: the NPC offers a hardcore character only the drop" % spawn.place,
                          menu is not None and len(menu.options) == 1 and menu.option("drop") is not None, repr(menu))
        else:
            results.check("%s: the NPC offers a hardcore character the drop, not the status" % spawn.place,
                          menu is not None and menu.option("drop") is not None and menu.option("hardcore") is None,
                          repr(menu))
    hardcore.gm(".gm off")
    results.check("the tour dropped nothing", status_row(realm, hardcore) == "1", status_row(realm, hardcore))
    close(hardcore)


def part_drop():
    """Drops the status of a new character at the Valley of Trials NPC. Returns the logged-in client."""
    client = login("Hcdrop" + tag, "PLAYER255C")
    results.check("a new character takes the status", take_hardcore(realm, client))
    menu, npc = greeting(client)
    drop = menu.option("drop") if menu else None
    results.check("the NPC offers the drop to a hardcore character that is alive", drop is not None, repr(menu))
    warning = client.gossip_select(npc, drop) if drop is not None else None
    results.check("the drop option opens the warning page", warning is not None
                  and warning.text_id == HARDCORE_TEXT_DROP_WARNING, repr(warning))
    results.check("the warning page asks for a confirmation", warning is not None and warning.option("yes") is not None
                  and warning.option("no") is not None, repr(warning))
    results.check("the status stays until the confirmation", status_row(realm, client) == "1", status_row(realm, client))
    start = len(client.chat_log)
    closed = (client.gossip_select(npc, warning.option("yes"))
              if warning is not None and warning.option("yes") is not None else "no warning page")
    results.check("the confirmation closes the gossip", closed is None, repr(closed))
    results.check("the confirmation tells the player", any("dropped the hardcore status" in line
                  for line in system_lines(client, start)), system_lines(client, start))
    results.check("the drop is written at once (status 3)", status_row(realm, client) == "3", status_row(realm, client))
    dropped_at = realm.sql("characters", "SELECT dropped_at FROM character_hardcore WHERE guid = %d" % low(client))
    results.check("the drop time is written", dropped_at.isdigit() and int(dropped_at) > 0, dropped_at)
    results.check(".hardcore says that the character was hardcore before", hardcore_line(client) == DROPPED,
                  hardcore_line(client))
    menu, _ = greeting(client)
    results.check("the NPC offers neither the status nor the drop again", menu is not None and not menu.options,
                  repr(menu))
    results.check("the status cannot be taken again", not take_hardcore(realm, client)
                  and status_row(realm, client) == "3")
    start = len(client.chat_log)
    client.relog()
    results.check("no Hardcore: alive at login after the drop", ALIVE not in system_lines(client, start),
                  system_lines(client, start))
    menu, _ = greeting(client)
    results.check("after a relog the NPC still offers nothing", menu is not None and not menu.options, repr(menu))
    results.check("after a relog .hardcore still says dropped", hardcore_line(client) == DROPPED, hardcore_line(client))
    return client


def full_guid(high, entry, guid_low):
    return (high << 48) | (entry << 24) | guid_low


def part_after(dropped=None):
    """A dropped character is a normal character: trade, mail, group, auction house."""
    dropped = dropped or part_drop()
    normal = login("Hcnorm" + tag, "PLAYER255D")
    mailbox_entry = int(realm.sql("mangos", "SELECT id FROM gameobject WHERE guid = %d" % ORGRIMMAR_MAILBOX))
    mailbox = full_guid(0xF110, mailbox_entry, ORGRIMMAR_MAILBOX)
    place = ".go xyz 1617.0 -4391.6 10.2 1"  # by the Orgrimmar mailbox, in reach of it and of each other
    for client in (dropped, normal):
        client.gm(place, wait=3.0)

    results.check("trade: the dropped character opens a trade with a normal one", dropped.trade_open(normal.guid))
    dropped.trade_cancel()
    results.check("trade: a normal character opens a trade with the dropped one", normal.trade_open(dropped.guid))
    normal.trade_cancel()

    sent = normal.send_mail(mailbox, name_of(dropped), "after the drop", "t255")
    results.check("mail: a normal character mails the dropped one", sent == 0, sent)
    sent = dropped.send_mail(mailbox, name_of(normal), "after the drop", "t255")
    results.check("mail: the dropped character mails a normal one", sent == 0, sent)
    arrived = realm.sql("characters", "SELECT COUNT(*) FROM mail WHERE receiver = %d AND sender = %d"
                        % (low(dropped), low(normal)))
    results.check("mail: the mail reaches the dropped character", arrived not in ("", "0"), arrived)

    guids = realm.sql("mangos", "SELECT guid FROM creature WHERE id = %d" % ORGRIMMAR_AUCTIONEER).split()
    dropped.gm(".go xyz 1684.0 -4463.0 20.15 1", wait=3.0)
    opened = any(dropped.auction_hello(full_guid(0xF130, ORGRIMMAR_AUCTIONEER, int(g))) for g in guids)
    results.check("auction house: the dropped character opens it", opened, guids)

    dropped.gm(place, wait=3.0)
    party = dropped.group_invite(name_of(normal))
    normal.group_accept()
    normal.pump(1)
    results.check("group: the dropped character invites a normal one", party == 0 and same_group(dropped, normal), party)
    normal.group_leave()
    dropped.pump(1)
    party = normal.group_invite(name_of(dropped))
    dropped.group_accept()
    dropped.pump(1)
    results.check("group: a normal character invites the dropped one", party == 0 and same_group(dropped, normal), party)
    dropped.group_leave()

    menu, _ = greeting(dropped)
    results.check("after all that the NPC still does not offer the status", menu is not None
                  and menu.option("hardcore") is None, repr(menu))
    close(dropped)
    close(normal)


def same_group(a, b):
    rows = realm.sql("characters", "SELECT group_id FROM group_member WHERE member_guid IN (%d, %d)" % (low(a), low(b)))
    ids = rows.split()
    return len(ids) == 2 and ids[0] == ids[1]


def part_group():
    """One drop call drops every hardcore member of the group (two hardcore players here; #257 with bots)."""
    leader = login("Hclead" + tag, "PLAYER255E")
    member = login("Hcmemb" + tag, "PLAYER255F")
    results.check("the leader takes the status", take_hardcore(realm, leader))
    results.check("the member takes the status", take_hardcore(realm, member))
    party = leader.group_invite(name_of(member))
    member.group_accept()
    member.pump(1)
    results.check("two hardcore characters group", party == 0 and same_group(leader, member), party)
    start = len(member.chat_log)
    results.check("the leader drops the status at the Orgrimmar NPC (a capital)",
                  drop_hardcore(realm, leader, hardcore_spawn("Orgrimmar")))
    member.pump(1)
    results.check("the hardcore member dropped it at the same moment", status_row(realm, member) == "3",
                  status_row(realm, member))
    results.check("the member is told in its chat", any("dropped the hardcore status" in line
                  for line in system_lines(member, start)), system_lines(member, start))
    results.check(".hardcore of the member says dropped", hardcore_line(member) == DROPPED, hardcore_line(member))
    leader.group_leave()
    close(leader)
    close(member)


def part_dead():
    """A dead hardcore character gets no drop (#253 makes the death final, status 2)."""
    client = login("Hcdead" + tag, "PLAYER255G")
    results.check("a new character takes the status (dead)", take_hardcore(realm, client))
    go_to_hardcore_npc(client)
    kill(client, client.guid)
    client.pump(2)
    results.check("the death is final (status 2)", status_row(realm, client) == "2", status_row(realm, client))
    menu = client.gossip_hello(HARDCORE_NPC)
    results.check("the NPC does not offer the drop to a dead hardcore character",
                  menu is None or menu.option("drop") is None, repr(menu))
    results.check("the dead character stays dead", status_row(realm, client) == "2", status_row(realm, client))
    close(client)


realm = Realm()
results = Results()
tag = suffix()
parts = {"spawns": part_spawns, "drop": lambda: close(part_drop()), "after": part_after, "group": part_group,
         "dead": part_dead}
chosen = sys.argv[1:] or ["spawns", "after", "group", "dead"]  # "after" runs "drop" first
for name in chosen:
    print("== part %s" % name)
    parts[name]()
results.finish()
