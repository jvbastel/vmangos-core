#!/usr/bin/env python3
"""In-game check of jvbastel/WOWAI#254 (spec #250, user stories 26-33): no trade, mail, auction house or group
across the hardcore border. One run on a slot with a build that has #251 and #254:

    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t254_hardcore_border.py

Four player characters (orc warriors, Horde): two hardcore (the "hardcore" and the "hardcore friend") and two
that are not (the "alt" and the "other alt"). Each run makes new characters (a name suffix from the time), so
it can run again on the same lease. The characters meet at the mailbox nearest to Razor Hill.

1. Across the border (hardcore and alt): trade is refused in both directions, mail from the alt to the
   hardcore character is refused (text, money, item, cash on delivery), cash on delivery mail from the
   hardcore character to the alt is refused, and group invite is refused in both directions. Each refusal
   tells the character that asked, with its hardcore reason, and the other character sees nothing.
   The hardcore character cannot open the auction house; the alt can.
2. Mail sent by the game (GM `.send items` and `.send money` over SOAP) reaches the hardcore character, and it
   takes the item and the money at the mailbox.
3. Two hardcore characters: trade (with gold), mail (with money) and group invite work.
4. The hardcore character joins the alt's guild (a 1.12 guild moves no items or gold).
5. Two characters that are not hardcore keep the normal trade, mail and group invite.
"""
import time

from kit import Realm, Results, take_hardcore
from wowclient import (AuctionClient, CHAT_MSG_SYSTEM, ERR_PARTY_RESULT_OK, GE_JOINED, GossipClient, GroupClient,
                       GuildClient, MAIL_ERR_INTERNAL_ERROR, MAIL_ITEM_TAKEN, MAIL_MONEY_TAKEN, MAIL_OK, MAIL_SEND,
                       MailClient, TRADE_STATUS_BEGIN_TRADE, TRADE_STATUS_OPEN_WINDOW, TRADE_STATUS_TRADE_COMPLETE,
                       TradeClient)

# The refusal reasons (src/game/Hardcore/HardcoreRules.cpp).
NO_TRADE = "Hardcore: a hardcore character cannot trade with a character that is not hardcore."
NO_MAIL = "Hardcore: a character that is not hardcore cannot send mail to a hardcore character."
NO_COD = "Hardcore: a hardcore character cannot send cash on delivery mail to a character that is not hardcore."
NO_AUCTION = "Hardcore: a hardcore character cannot use the auction house."
NO_GROUP = "Hardcore: a hardcore character cannot be in a group with a character that is not hardcore."

WATER = 159  # Refreshing Spring Water: a plain item to mail and trade
RAZOR_HILL = (315.0, -4740.0)
ORGRIMMAR = (1630.0, -4400.0)
AUCTIONEER_FLAG = 0x1000  # UNIT_NPC_FLAG_AUCTIONEER
MAILBOX_TYPE = 19  # GAMEOBJECT_TYPE_MAILBOX


class Client(GossipClient, TradeClient, MailClient, AuctionClient, GroupClient, GuildClient):
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


def system_lines(client, start):
    return [text for (t, _, text) in client.chat_log[start:] if t == CHAT_MSG_SYSTEM]


def nearest(realm, query, place):
    """(low guid, entry, x, y, z) of the first row of `query` (columns guid, id, x, y, z on map 1) nearest to
    `place`."""
    order = " ORDER BY POW(position_x - %f, 2) + POW(position_y - %f, 2) LIMIT 1" % place
    row = realm.sql("mangos", query + order).split("\t")
    return int(row[0]), int(row[1]), float(row[2]), float(row[3]), float(row[4])


def go_to(client, where):
    """GM-teleports the character 1.5 yards west of a place (x, y, z, ...) on Kalimdor."""
    client.gm(".go xyz %.1f %.1f %.1f 1" % (where[0] - 1.5, where[1], where[2] + 0.5), wait=3.0)


def refused(name, asker, other, action, reason, seen):
    """Runs `action` on `asker`; checks that `asker` got `reason` and that `seen(other, start)` is empty."""
    start_asker, mark = len(asker.chat_log), seen(other, None)
    action()
    other.pump(0.5)
    lines = system_lines(asker, start_asker)
    results.check(name + ": refused with its reason", reason in lines, lines)
    results.check(name + ": the other character sees nothing", not seen(other, mark), seen(other, mark))


def trade_seen(client, mark):
    """With mark None: a mark. Else the trade requests since the mark."""
    if mark is None:
        return len(client.trade_statuses)
    return [s for s in client.trade_statuses[mark:] if s in (TRADE_STATUS_BEGIN_TRADE, TRADE_STATUS_OPEN_WINDOW)]


def invite_seen(client, mark):
    if mark is None:
        return len(client.group_invites)
    return client.group_invites[mark:]


def mail_rows(realm, sender, receiver):
    """The count of mails in the mail table from `sender` to `receiver` (clients)."""
    return int(realm.sql("characters", "SELECT COUNT(*) FROM mail WHERE sender_guid = %d AND receiver_guid = %d"
                         % (low(sender), low(receiver))) or 0)


def item_guid(realm, client, entry):
    """The full guid of an item `entry` in the character's bags (after a `.save`), or 0."""
    client.gm(".save", select=client.guid)
    client.pump(1)
    row = realm.sql("characters", "SELECT item_guid FROM character_inventory WHERE guid = %d AND item_id = %d "
                    "ORDER BY item_guid LIMIT 1" % (low(client), entry))
    return (0x4000 << 48) | int(row) if row else 0


def money(realm, client):
    client.gm(".save", select=client.guid)
    client.pump(1)
    return int(realm.sql("characters", "SELECT money FROM characters WHERE guid = %d" % low(client)) or 0)


def trade_gold(giver, taker, copper):
    """A full trade: `giver` asks, `taker` says yes, `giver` puts in gold, both accept. The statuses of both."""
    giver.trade_initiate(taker.guid)
    taker.pump(0.5)
    taker.trade_begin()
    giver.pump(0.5)
    giver.trade_gold(copper)
    taker.pump(0.5)
    giver.trade_accept()
    taker.trade_accept()
    giver.pump(1)
    return giver.trade_statuses, taker.trade_statuses


def group_join(leader, member):
    """`leader` invites `member`, who accepts. True when the leader's group list holds the member."""
    results_ = leader.group_invite(member.names[member.guid])
    member.pump(0.5)
    if not member.group_invites:
        return False, results_
    member.group_accept()
    leader.pump(1)
    return member.guid in leader.group_members, results_


def close(client):
    try:
        client.logout()
    except Exception:
        pass
    client.sock.close()


realm = Realm()
results = Results()
tag = suffix()

hardcore = realm.login("PLAYER254A", "Hcbord" + tag, client_class=Client)
friend = realm.login("PLAYER254B", "Hcfrnd" + tag, client_class=Client)
alt = realm.login("PLAYER254C", "Altbord" + tag, client_class=Client)
other = realm.login("PLAYER254D", "Altothr" + tag, client_class=Client)
clients = (hardcore, friend, alt, other)
names = {c.guid: c.names[c.guid] for c in clients}
for c in clients:
    c.names.update(names)

results.check("the hardcore character takes the status", take_hardcore(realm, hardcore))
results.check("the hardcore friend takes the status", take_hardcore(realm, friend))

mailbox_low, mailbox_entry, *mailbox_place = nearest(
    realm, "SELECT g.guid, g.id, g.position_x, g.position_y, g.position_z FROM gameobject g "
    "JOIN gameobject_template t ON t.entry = g.id WHERE g.map = 1 AND t.type = %d GROUP BY g.guid" % MAILBOX_TYPE,
    RAZOR_HILL)
MAILBOX = (0xF110 << 48) | (mailbox_entry << 24) | mailbox_low
for c in clients:
    c.gm(".gm off")
    c.gm(".modify money 100000", select=c.guid)
    c.gm(".additem %d 5" % WATER, select=c.guid)
    go_to(c, mailbox_place)

# 1. Across the border.
refused("trade from the hardcore character to the alt", hardcore, alt,
        lambda: hardcore.trade_initiate(alt.guid), NO_TRADE, trade_seen)
refused("trade from the alt to the hardcore character", alt, hardcore,
        lambda: alt.trade_initiate(hardcore.guid), NO_TRADE, trade_seen)

hardcore_name, alt_name = names[hardcore.guid], names[alt.guid]
for what, kwargs in (("text", {}), ("money", {"money": 100}), ("an item", {"item": item_guid(realm, alt, WATER)}),
                     ("cash on delivery", {"item": item_guid(realm, alt, WATER), "cod": 100})):
    start, before = len(alt.chat_log), mail_rows(realm, alt, hardcore)
    answers = alt.send_mail(MAILBOX, hardcore_name, "Help " + tag, "From your alt.", **kwargs)
    results.check("mail with %s from the alt to the hardcore character is refused" % what,
                  answers == [(0, MAIL_SEND, MAIL_ERR_INTERNAL_ERROR)], answers)
    results.check("mail with %s: the alt gets the reason" % what, NO_MAIL in system_lines(alt, start), system_lines(alt, start))
    results.check("mail with %s: no mail is stored" % what, mail_rows(realm, alt, hardcore) == before)

start = len(hardcore.chat_log)
answers = hardcore.send_mail(MAILBOX, alt_name, "Pay " + tag, "Cash on delivery.",
                             item=item_guid(realm, hardcore, WATER), cod=100)
results.check("cash on delivery mail from the hardcore character to the alt is refused, with its reason",
              answers == [(0, MAIL_SEND, MAIL_ERR_INTERNAL_ERROR)] and NO_COD in system_lines(hardcore, start),
              "%s %s" % (answers, system_lines(hardcore, start)))

refused("group invite from the hardcore character to the alt", hardcore, alt,
        lambda: hardcore.group_invite(alt_name), NO_GROUP, invite_seen)
refused("group invite from the alt to the hardcore character", alt, hardcore,
        lambda: alt.group_invite(hardcore_name), NO_GROUP, invite_seen)
results.check("no group was formed across the border", not hardcore.group_members and not alt.group_members)

auctioneer_low, auctioneer_entry, *auctioneer_place = nearest(
    realm, "SELECT c.guid, c.id, c.position_x, c.position_y, c.position_z FROM creature c "
    "JOIN creature_template t ON t.entry = c.id WHERE c.map = 1 AND t.npc_flags & %d GROUP BY c.guid" % AUCTIONEER_FLAG,
    ORGRIMMAR)
AUCTIONEER = (0xF130 << 48) | (auctioneer_entry << 24) | auctioneer_low
for c in (hardcore, alt):
    go_to(c, auctioneer_place)
start = len(hardcore.chat_log)
house = hardcore.auction_hello(AUCTIONEER)
results.check("the hardcore character cannot open the auction house", house is None, house)
results.check("the auction house refusal has its reason", NO_AUCTION in system_lines(hardcore, start), system_lines(hardcore, start))
house = alt.auction_hello(AUCTIONEER)
results.check("the alt opens the auction house (the auctioneer answers)", house is not None and house[0] == AUCTIONEER, house)
for c in (hardcore, alt):
    go_to(c, mailbox_place)

# 2. Mail sent by the game reaches the hardcore character.
before_notes = hardcore.new_mail
realm.soap('.send items %s "Game %s" "Sent by the game." %d:1' % (hardcore_name, tag, WATER))
realm.soap('.send money %s "Gold %s" "Sent by the game." 250' % (hardcore_name, tag))
hardcore.pump(2)
rows = realm.sql("characters", "SELECT id, subject FROM mail WHERE receiver_guid = %d AND subject IN ('Game %s', 'Gold %s')"
                 % (low(hardcore), tag, tag))
game_mail = dict(reversed(row.split("\t")) for row in rows.splitlines() if row)
results.check("the game's item and money mail reach the hardcore character", len(game_mail) == 2, rows)
results.check("the hardcore character gets the new mail note", hardcore.new_mail > before_notes)
taken = hardcore.mail_take_item(MAILBOX, int(game_mail.get("Game " + tag, 0)))
results.check("the hardcore character takes the item of the game's mail",
              any(action == MAIL_ITEM_TAKEN and error == MAIL_OK for _, action, error in taken), taken)
before = money(realm, hardcore)
taken = hardcore.mail_take_money(MAILBOX, int(game_mail.get("Gold " + tag, 0)))
results.check("the hardcore character takes the money of the game's mail",
              any(action == MAIL_MONEY_TAKEN and error == MAIL_OK for _, action, error in taken)
              and money(realm, hardcore) == before + 250, "%s, %d -> %d" % (taken, before, money(realm, hardcore)))

# 3. Two hardcore characters.
before = money(realm, friend)
giver, taker = trade_gold(hardcore, friend, 300)
results.check("trade between two hardcore characters opens", TRADE_STATUS_OPEN_WINDOW in giver and TRADE_STATUS_OPEN_WINDOW in taker,
              "%s / %s" % (giver, taker))
results.check("trade between two hardcore characters completes", TRADE_STATUS_TRADE_COMPLETE in taker, taker)
results.check("the gold of the trade arrives", money(realm, friend) == before + 300)
before = mail_rows(realm, friend, hardcore)
answers = friend.send_mail(MAILBOX, hardcore_name, "Hc mail " + tag, "From a hardcore friend.", money=100)
results.check("mail with money between two hardcore characters is sent", answers == [(0, MAIL_SEND, MAIL_OK)], answers)
results.check("the hardcore mail is stored", mail_rows(realm, friend, hardcore) == before + 1)
joined, party = group_join(hardcore, friend)
results.check("group invite between two hardcore characters works", joined, "%s %s" % (party, hardcore.group_members))
results.check("the invite answer is ok", (0, names[friend.guid], ERR_PARTY_RESULT_OK) in party, party)
friend.group_leave()
hardcore.pump(1)

# 4. Guild: the hardcore character joins the alt's guild.
guild = "Border" + tag.capitalize()
realm.soap('.guild create %s "%s"' % (alt_name, guild))
alt.pump(1)
alt.guild_invite(hardcore_name)
hardcore.pump(1)
results.check("the hardcore character gets the alt's guild invite", any(g == guild for _, g in hardcore.guild_invites),
              hardcore.guild_invites)
hardcore.guild_accept()
alt.pump(1)
row = realm.sql("characters", "SELECT g.name FROM guild_member m JOIN guild g ON g.guild_id = m.guild_id WHERE m.guid = %d"
                % low(hardcore))
results.check("the hardcore character is in a guild with a character that is not hardcore", row == guild, row)
results.check("the guild sees the hardcore character join",
              any(e == GE_JOINED and hardcore_name in s for e, s in alt.guild_events), alt.guild_events)

# 5. Two characters that are not hardcore.
before = money(realm, other)
giver, taker = trade_gold(alt, other, 300)
results.check("trade between two characters that are not hardcore completes", TRADE_STATUS_TRADE_COMPLETE in taker
              and money(realm, other) == before + 300, "%s / %s" % (giver, taker))
before = mail_rows(realm, alt, other)
answers = alt.send_mail(MAILBOX, names[other.guid], "Alt mail " + tag, "From an alt.", money=100)
results.check("mail with money between two characters that are not hardcore is sent",
              answers == [(0, MAIL_SEND, MAIL_OK)] and mail_rows(realm, alt, other) == before + 1, answers)
start = len(alt.chat_log)
joined, party = group_join(alt, other)
results.check("group invite between two characters that are not hardcore works", joined, "%s %s" % (party, alt.group_members))
results.check("no hardcore line for characters that are not hardcore",
              not [line for line in system_lines(alt, start) if line.startswith("Hardcore:")])
other.group_leave()

for c in clients:
    close(c)
results.finish()
