#!/usr/bin/env python3
"""In-game check of jvbastel/WOWAI#257 (spec #250): hardcore bots in a hardcore character's group. One run on
a slot with the #251-#257 build:

    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t257_hardcore_bots.py

The parts run in this order on one hardcore player (each run makes new characters, with a name suffix from
the time, so it can run again on the same lease). The bots are named by role here: the tank, the healer
and the ranged member.

1. make: a new character takes the hardcore status and goes to level 10 (GM). It makes a hardcore tank,
   healer and ranged member with `.wowai bot create ... hardcore [level]` (the ranged member below its
   level); each is level <= 10, has a character_hardcore row (alive), and wears template gear. A level
   above the player's (11) is refused, and no character is made. A hardcore bot refuses a group invite of a
   character that is not hardcore. The player invites the three (whisper `invite`).
2. border: a bot that is not hardcore cannot join: the whisper `invite` and a /invite packet are refused
   with the hardcore reason, and it is in no group with the player. A stock party bot (`.partybot add`,
   which skips the invite handler) is refused by the group rule of Group::AddMember (#250 final review): the
   hardcore reason, the log line, and no new member in the group.
2b. level (#250 final review, story 39): a hardcore bot that is above the player's level now (made at its
   level, then levelled up by a GM) cannot join: the whisper `invite` and a /invite packet are refused.
3. trade: the player trades gold with the healer (the bot opens and accepts the trade); mail from the
   player reaches the healer.
4. loot: the group loots a Mottled Boar with fixed green loot (set by SQL, put back at the end): group loot
   (the bots roll need or greed, the player passes, a bot wins), then master loot (the player gives an item
   to the tank).
5. death: the ranged member dies (GM `.die`). It does not release, the healer does not try to resurrect it,
   a GM `.revive` does not bring it back, it has a death record and the lock flag, and it leaves the group
   and logs out about 60 seconds after its death. Then the bot commands refuse it: `.wowai bot login`, the
   whisper `invite`, and `.wowai bot create` with its name.
6. drop: the player drops the status at the Valley of Trials NPC; the tank and the healer are dropped at
   the same moment (same dropped_at). The dead ranged member stays dead (status 2).
7. restart: after a server restart, the dead ranged member is not logged in, and `.wowai bot login` still
   refuses it.
8. players (#250 final review), on new level 1 characters:
   - take: a character in a group with a character that is not hardcore cannot take the status at the NPC
     (the reason, no row); after it leaves the group it can.
   - accept: a character that took the status with an invite open from a character that is not hardcore
     cannot accept it (Group::AddMember refuses, with the reason).
   - drop: in a group of two hardcore players, the NPC refuses the drop (the reason; both stay alive).
   - dead invite: a dead hardcore player (its 60 seconds, not in a group) cannot be invited (the "dead for
     good" reason).
   - player death: a dead hardcore player leaves its group at its 60-second logout, so its hardcore bot
     leaves the group and logs out too.
"""
import re
import struct
import time

import wowclient

from kit import (BOAR, HARDCORE_ALIVE, HARDCORE_DEAD, HARDCORE_DROPPED, HARDCORE_REASON_BOT_ABOVE_LEVEL,
                 HARDCORE_REASON_DEAD, HARDCORE_REASON_DROP_WITH_PLAYER, HARDCORE_REASON_NO_GROUP,
                 HARDCORE_REASON_TAKE_IN_GROUP, PLAYER_FLAGS_GHOST, Realm, Results, drop_hardcore, hardcore_locked,
                 hardcore_row, kill, spawn, stat, take_hardcore, unit_field)
from kit import online as kit_online
from wowclient import CHAT_MSG_PARTY, CHAT_MSG_SYSTEM, CHAT_MSG_WHISPER, GossipClient
# The trade, mail and group packets of #254.
from wowclient import (MAIL_OK, MAIL_SEND, TRADE_STATUS_OPEN_WINDOW, TRADE_STATUS_TRADE_COMPLETE, GroupClient,
                       MailClient, TradeClient)

CMSG_LOOT_METHOD = 122
CMSG_LOOT = 349
CMSG_LOOT_RELEASE = 351
SMSG_LOOT_RESPONSE = 352
SMSG_LOOT_REMOVED = 354
SMSG_LOOT_ROLL_WON = 671
CMSG_LOOT_ROLL = 672
SMSG_LOOT_START_ROLL = 673
CMSG_LOOT_MASTER_GIVE = 675
MASTER_LOOT, GROUP_LOOT = 2, 3
UNCOMMON = 2
ROLL_PASS = 0

PLAYER_LEVEL = 10
# Fixed green loot of the Mottled Boar (as in #41): War Paint Legguards and a green pair of pants.
LOOT_ITEMS = (14727, 2974)
ORGRIMMAR_MAILBOX = 10107  # gameobject guid: the mailbox by the Orgrimmar bank
ORGRIMMAR_MAIL_SPOT = ".go xyz 1617.0 -4391.6 10.2 1"
VALLEY_SPOT = ".go xyz -600.0 -4250.0 38.9 1"

NO_GROUP = HARDCORE_REASON_NO_GROUP
DEAD_BOT = HARDCORE_REASON_DEAD
ABOVE_LEVEL = HARDCORE_REASON_BOT_ABOVE_LEVEL
TAKE_IN_GROUP = HARDCORE_REASON_TAKE_IN_GROUP
DROP_WITH_PLAYER = HARDCORE_REASON_DROP_WITH_PLAYER


class Client(GossipClient, TradeClient, MailClient, GroupClient):
    """The hardcore player. It passes on each group loot roll and keeps the roll winners, and keeps the loot
    windows and the removed loot slots."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.roll_wins = []
        self.loot_opened = []
        self.loot_removed = []

    def _observe(self, opcode, body):
        super()._observe(opcode, body)
        if opcode == SMSG_LOOT_START_ROLL and len(body) >= 12:
            target, slot = struct.unpack_from("<QI", body, 0)
            self.send(CMSG_LOOT_ROLL, struct.pack("<QIB", target, slot, ROLL_PASS))
        elif opcode == SMSG_LOOT_ROLL_WON and len(body) >= 34:
            _, _, entry, _, _, winner, _, kind = struct.unpack_from("<QIIIIQBB", body, 0)
            self.roll_wins.append((entry, winner, kind))
        elif opcode == SMSG_LOOT_RESPONSE and len(body) >= 8:
            self.loot_opened.append(struct.unpack_from("<Q", body, 0)[0])
        elif opcode == SMSG_LOOT_REMOVED and len(body) >= 1:
            self.loot_removed.append(body[0])


def suffix():
    """Four lowercase letters from the time, so that each run has new names."""
    n, out = int(time.time()), ""
    for _ in range(4):
        n, k = divmod(n, 26)
        out += chr(ord("a") + k)
    return out


def low(guid):
    return guid & 0xFFFFFFFF


def system_lines(client, start):
    return [text for (t, _, text) in client.chat_log[start:] if t == CHAT_MSG_SYSTEM]


def said(client, start, words):
    return any(words in line for line in system_lines(client, start))


def status_of(guid):
    """The status column of the character_hardcore row ("" without a row)."""
    row = hardcore_row(realm, guid)
    return row[0] if row else ""


def in_group_with(a, b):
    rows = realm.sql("characters", "SELECT group_id FROM group_member WHERE member_guid IN (%d, %d)" % (low(a), low(b)))
    ids = rows.split()
    return len(ids) == 2 and ids[0] == ids[1]


def online(guid):
    return kit_online(realm, guid)


def field(guid, name):
    """An update field of a unit (GM `.debug getvaluebyname`), or -1."""
    return unit_field(player, guid, name)


def soap(command):
    """A GM command over SOAP; its answer, or the error text when the command fails."""
    try:
        return realm.soap(command)
    except RuntimeError as error:
        return str(error)


def bring(names):
    """The bots stand with the player, and follow."""
    for name in names:
        player.gm(".namego %s" % name, wait=1.0)
    player.chat(CHAT_MSG_PARTY, "follow")
    player.pump(3)


def item_count(guid, entries):
    player.gm(".saveall", wait=2.0)
    row = realm.sql("characters", "SELECT COUNT(*) FROM character_inventory WHERE guid = %d AND item_id IN (%s)"
                    % (low(guid), ", ".join(str(e) for e in entries)))
    return int(row or 0)


def make_bot(name, words):
    """`.wowai bot create <name> <words>` as the player. The answer lines."""
    return player.gm(".wowai bot create %s %s" % (name, words), wait=2.0)


# --- 1. make ---

def part_make():
    results.check("the player takes the hardcore status", take_hardcore(realm, player))
    player.gm(".levelup %d" % (PLAYER_LEVEL - 1), select=player.guid)
    player.gm(".save", select=player.guid)
    level = realm.sql("characters", "SELECT level FROM characters WHERE guid = %d" % low(player.guid))
    results.check("the hardcore player is level %d" % PLAYER_LEVEL, level == str(PLAYER_LEVEL), level)

    realm.mark("make")
    answer = make_bot(tank, "warrior tank hardcore")
    if any("Forever rules" in line for line in answer):
        print("  the Forever rules are on: the tank is a Druid")
        answer = make_bot(tank, "druid tank tauren hardcore")
    results.check("the player makes a hardcore tank at its level", any("Hardcore bot %s made at level %d" % (tank, PLAYER_LEVEL)
                  in line for line in answer), answer)
    answer = make_bot(healer, "priest healer hardcore %d" % PLAYER_LEVEL)
    results.check("the player makes a hardcore healer at its level", any("Hardcore bot %s made at level %d"
                  % (healer, PLAYER_LEVEL) in line for line in answer), answer)
    answer = make_bot(ranged, "mage ranged troll hardcore %d" % (PLAYER_LEVEL - 2))
    results.check("the player makes a hardcore ranged member below its level", any("Hardcore bot %s made at level %d"
                  % (ranged, PLAYER_LEVEL - 2) in line for line in answer), answer)
    answer = make_bot(over, "mage ranged troll hardcore %d" % (PLAYER_LEVEL + 1))
    results.check("a hardcore bot above the player's level is refused",
                  any("cannot be above your level (%d)" % PLAYER_LEVEL in line for line in answer), answer)
    results.check("the refused bot is not made", realm.guid_of(over) == 0)

    for name in (tank, healer, ranged):
        guids[name] = realm.guid_of(name)
        results.check("%s: logs in" % role[name], realm.wait_log(r"\[Bots\] %s logs in" % name, 20, player))
    player.pump(3)
    player.gm(".saveall", wait=2.0)
    for name, wanted in ((tank, PLAYER_LEVEL), (healer, PLAYER_LEVEL), (ranged, PLAYER_LEVEL - 2)):
        guid = guids[name]
        level = realm.sql("characters", "SELECT level FROM characters WHERE guid = %d" % guid)
        results.check("%s: level %d" % (role[name], wanted), level == str(wanted), level)
        results.check("%s: hardcore (alive)" % role[name], status_of(guid) == HARDCORE_ALIVE, status_of(guid))
        taken = realm.sql("characters", "SELECT taken_at FROM character_hardcore WHERE guid = %d" % guid)
        results.check("%s: the time of the status is written" % role[name], taken.isdigit() and int(taken) > 0, taken)
        worn = realm.sql("characters", "SELECT COUNT(*) FROM character_inventory WHERE guid = %d AND bag = 0 "
                         "AND slot < 19" % guid)
        results.check("%s: wears template gear" % role[name], int(worn or 0) >= 3, worn)

    # A hardcore bot refuses a character that is not hardcore.
    start = len(normal.chat_log)
    normal.chat(CHAT_MSG_WHISPER, "invite", target=healer)
    normal.pump(3)
    results.check("a character that is not hardcore cannot invite a hardcore bot", said(normal, start, NO_GROUP)
                  and not in_group_with(normal.guid, guids[healer]), system_lines(normal, start))

    for name in (tank, healer, ranged):
        player.chat(CHAT_MSG_WHISPER, "invite", target=name)
        player.pump(4)
        results.check("%s: joins the hardcore player's group" % role[name], in_group_with(player.guid, guids[name]))
    bring((tank, healer, ranged))


# --- 2. border ---

def normal_bot(wanted_role):
    """The first Horde roster bot of the role that has no character_hardcore row."""
    rows = realm.sql("characters", "SELECT c.name FROM characters c JOIN bot_character b ON b.guid = c.guid "
                     "LEFT JOIN character_hardcore h ON h.guid = c.guid WHERE b.world_bot = 0 AND h.guid IS NULL "
                     "AND b.role = '%s' AND c.race IN (2, 5, 6, 8) ORDER BY c.name" % wanted_role)
    names = rows.split()
    return names[0] if names else None


def part_border():
    name = normal_bot("healer") or normal_bot("ranged")
    if not results.check("the realm has a bot that is not hardcore", name is not None):
        return
    guid = realm.guid_of(name)
    start = len(player.chat_log)
    player.chat(CHAT_MSG_WHISPER, "invite", target=name)
    player.pump(4)
    results.check("whisper invite: a bot that is not hardcore is refused with the hardcore reason",
                  said(player, start, NO_GROUP), system_lines(player, start))
    results.check("whisper invite: it is not in the hardcore group", not in_group_with(player.guid, guid))

    print("  login:", soap(".wowai bot login %s" % name))
    player.pump(5)
    start = len(player.chat_log)
    party = player.group_invite(name)
    player.pump(3)
    results.check("/invite: a bot that is not hardcore is refused with the hardcore reason",
                  said(player, start, NO_GROUP), (party, system_lines(player, start)))
    results.check("/invite: it is not in the hardcore group", not in_group_with(player.guid, guid))
    print("  logout:", soap(".wowai bot logout %s" % name))


    # A stock party bot does not pass the invite handler or the bot manager: Group::AddMember refuses it.
    realm.mark("partybot")
    members = realm.sql("characters", "SELECT COUNT(*) FROM group_member WHERE group_id = (SELECT group_id FROM "
                        "group_member WHERE member_guid = %d)" % low(player.guid))
    start = len(player.chat_log)
    answer = player.gm(".partybot add priest", wait=3.0)
    player.pump(5)
    print("  partybot add:", answer)
    results.check("partybot: a stock party bot is refused with the hardcore reason", said(player, start, NO_GROUP),
                  system_lines(player, start))
    results.check("partybot: the server log has the refusal of Group::AddMember",
                  re.search(r"Hardcore: refused guid \d+ in the group of leader guid %d" % low(player.guid), realm.log())
                  is not None)
    after = realm.sql("characters", "SELECT COUNT(*) FROM group_member WHERE group_id = (SELECT group_id FROM "
                      "group_member WHERE member_guid = %d)" % low(player.guid))
    results.check("partybot: no new member in the hardcore group", after == members, (members, after))


# --- 2b. level ---

def part_level():
    answer = make_bot(climber, "warrior tank hardcore")
    results.check("level: the player makes a hardcore bot at its level", any("Hardcore bot %s made at level %d"
                  % (climber, PLAYER_LEVEL) in line for line in answer), answer)
    realm.wait_log(r"\[Bots\] %s logs in" % climber, 20, player)
    # In the world for the GM level-up (the online level is the one the rule reads).
    print("  login:", soap(".wowai bot login %s" % climber))
    player.pump(3)
    print("  levelup:", soap(".levelup %s 2" % climber))
    player.pump(2)
    guid = realm.guid_of(climber)
    start = len(player.chat_log)
    player.chat(CHAT_MSG_WHISPER, "invite", target=climber)
    player.pump(4)
    results.check("level: the whisper invite refuses a hardcore bot above the player's level",
                  said(player, start, ABOVE_LEVEL) and not in_group_with(player.guid, guid), system_lines(player, start))
    print("  login:", soap(".wowai bot login %s" % climber))
    player.pump(3)
    start = len(player.chat_log)
    party = player.group_invite(climber)
    player.pump(3)
    results.check("level: a /invite refuses a hardcore bot above the player's level",
                  said(player, start, ABOVE_LEVEL) and not in_group_with(player.guid, guid),
                  (party, system_lines(player, start)))
    print("  logout:", soap(".wowai bot logout %s" % climber))


# --- 3. trade and mail ---

def part_trade():
    bring((healer,))
    player.gm(".modify money 10000", select=player.guid)
    healer_guid = guids[healer]
    player.gm(".saveall", wait=2.0)
    money_before = int(realm.sql("characters", "SELECT money FROM characters WHERE guid = %d" % healer_guid) or 0)
    realm.mark("trade")
    asked = player.trade_initiate(healer_guid, wait=3.0)
    results.check("trade: the healer opens the player's trade", TRADE_STATUS_OPEN_WINDOW in asked + player.trade_statuses[-3:],
                  asked)
    player.trade_gold(500)
    done = player.trade_accept(wait=3.0)
    results.check("trade: the healer accepts and the trade completes", TRADE_STATUS_TRADE_COMPLETE in done
                  + player.trade_statuses[-3:], done)
    player.gm(".saveall", wait=2.0)
    money_after = int(realm.sql("characters", "SELECT money FROM characters WHERE guid = %d" % healer_guid) or 0)
    results.check("trade: the healer got the gold", money_after - money_before == 500, (money_before, money_after))
    results.check("trade: no hardcore refusal", "Hardcore:" not in realm.log())

    player.gm(ORGRIMMAR_MAIL_SPOT, wait=3.0)
    mailbox_entry = int(realm.sql("mangos", "SELECT id FROM gameobject WHERE guid = %d" % ORGRIMMAR_MAILBOX))
    mailbox = (0xF110 << 48) | (mailbox_entry << 24) | ORGRIMMAR_MAILBOX
    sent = player.send_mail(mailbox, healer, "for the group", "t257", money=100)
    results.check("mail: the player mails the healer", sent == [(0, MAIL_SEND, MAIL_OK)], sent)
    arrived = realm.sql("characters", "SELECT COUNT(*) FROM mail WHERE receiver = %d AND sender = %d"
                        % (healer_guid, low(player.guid)))
    results.check("mail: the mail reaches the healer", arrived not in ("", "0"), arrived)
    player.gm(VALLEY_SPOT, wait=3.0)
    bring((tank, healer, ranged))


# --- 4. loot ---

def set_boar_loot():
    realm.sql("mangos", "DROP TABLE IF EXISTS t257_loot_backup")
    realm.sql("mangos", "CREATE TABLE t257_loot_backup AS SELECT * FROM creature_loot_template WHERE entry = %d" % BOAR)
    realm.sql("mangos", "DELETE FROM creature_loot_template WHERE entry = %d; INSERT INTO creature_loot_template "
              "(entry, item, ChanceOrQuestChance, groupid, mincountOrRef, maxcount, condition_id, patch_min, patch_max) "
              "VALUES %s" % (BOAR, ", ".join("(%d, %d, 100, 0, 1, 1, 0, 0, 10)" % (BOAR, item) for item in LOOT_ITEMS)))
    print("  reload:", soap(".reload creature_loot_template"))


def put_back_boar_loot():
    realm.sql("mangos", "DELETE FROM creature_loot_template WHERE entry = %d; INSERT INTO creature_loot_template "
              "SELECT * FROM t257_loot_backup; DROP TABLE t257_loot_backup" % BOAR)
    print("  reload:", soap(".reload creature_loot_template"))


def loot_method(method):
    player.send(CMSG_LOOT_METHOD, struct.pack("<IQI", method, player.guid, UNCOMMON))
    player.pump(1)


def open_corpse(guid):
    player.loot_opened = []
    player.send(CMSG_LOOT, struct.pack("<Q", guid))
    player.pump(2)
    return guid in player.loot_opened


def part_loot():
    set_boar_loot()
    try:
        bots = {guids[name] for name in (tank, healer, ranged)}
        loot_method(GROUP_LOOT)
        realm.mark("group loot")
        player.roll_wins = []
        boar = spawn(realm, player, BOAR)
        kill(player, boar)
        player.pump(1)
        open_corpse(boar)
        player.send(CMSG_LOOT_RELEASE, struct.pack("<Q", boar))
        end = time.time() + 70
        while time.time() < end and len(player.roll_wins) < len(LOOT_ITEMS):
            player.pump(2)
        text = realm.log()
        rolled = [name for name in (tank, healer, ranged) if re.search(r"\[Bots\] %s rolls (need|greed)" % name, text)]
        results.check("group loot: the hardcore bots roll need or greed", len(rolled) >= 2,
                      [role[name] for name in rolled])
        results.check("group loot: a hardcore bot wins each roll", len(player.roll_wins) == len(LOOT_ITEMS)
                      and all(low(winner) in bots for _, winner, _ in player.roll_wins), player.roll_wins)

        loot_method(MASTER_LOOT)
        realm.mark("master loot")
        before = item_count(guids[tank], LOOT_ITEMS)
        boar = spawn(realm, player, BOAR)
        kill(player, boar)
        player.pump(1)
        results.check("master loot: the master looter opens the corpse", open_corpse(boar))
        player.loot_removed = []
        player.send(CMSG_LOOT_MASTER_GIVE, struct.pack("<QBQ", boar, 0, guids[tank]))
        player.pump(2)
        player.send(CMSG_LOOT_RELEASE, struct.pack("<Q", boar))
        player.pump(1)
        results.check("master loot: the item leaves the corpse", 0 in player.loot_removed, player.loot_removed)
        after = item_count(guids[tank], LOOT_ITEMS)
        results.check("master loot: the tank got the item", after == before + 1, (before, after))
        loot_method(GROUP_LOOT)
    finally:
        put_back_boar_loot()


# --- 5. death ---

def part_death():
    bring((tank, healer, ranged))
    guid = guids[ranged]
    realm.mark("death")
    print("  kill ranged member:", kill(player, guid))
    died_at = time.time()
    player.pump(2)
    row = realm.sql("characters", "SELECT status, death_time, death_zone, death_killer, death_level "
                    "FROM character_hardcore WHERE guid = %d" % guid).split("\t")
    results.check("death: the ranged member's death is final (status 2)", row[:1] == [HARDCORE_DEAD], row)
    results.check("death: the ranged member has a death record (time, zone, killer, level)", len(row) == 5
                  and row[1].isdigit() and int(row[1]) > 0 and row[2] not in ("", "0", "NULL") and row[3] not in ("", "NULL")
                  and row[4] == str(PLAYER_LEVEL - 2), row)
    results.check("death: the lock flag is written at once", hardcore_locked(realm, guid))

    print("  revive:", player.gm(".revive", select=guid))
    player.pump(2)
    hp = stat(player, guid)[0]
    results.check("death: a GM .revive does not bring it back", hp == 0, hp)
    player.pump(10)  # a bot that may release does it within seconds (#42)
    flags = field(guid, "PLAYER_FLAGS")
    results.check("death: no ghost in its 60 seconds", flags >= 0 and not flags & PLAYER_FLAGS_GHOST, flags)

    # The 60 seconds: the healer stands next to the dead body, out of combat.
    left = realm.wait_log(r"\[Bots\] %s is dead for good: it leaves its group and logs out" % ranged, 80, player)
    after = time.time() - died_at
    results.check("death: the ranged member leaves the group and logs out about 60 s after its death (%.0f s)" % after,
                  left and 55 <= after <= 80)
    text = realm.log()
    results.check("death: the ranged member does not release", not re.search(r"%s: release" % ranged, text)
                  and "refused the release of %s" % ranged not in text)
    results.check("death: the healer does not try to resurrect it", not re.search(r"%s: resurrect a member" % healer, text)
                  and "refused the resurrect of %s" % ranged not in text)
    player.pump(3)
    results.check("death: it is out of the group", not in_group_with(player.guid, guid))
    results.check("death: it is offline", not online(guid))

    answer = soap(".wowai bot login %s" % ranged)
    results.check("death: .wowai bot login refuses it", DEAD_BOT in answer, answer)
    start = len(player.chat_log)
    player.chat(CHAT_MSG_WHISPER, "invite", target=ranged)
    player.pump(4)
    results.check("death: the whisper invite refuses it", said(player, start, DEAD_BOT) and not online(guid),
                  system_lines(player, start))
    answer = make_bot(ranged, "mage ranged troll hardcore")
    results.check("death: .wowai bot create cannot make it again", any("exists already" in line for line in answer), answer)
    results.check("death: still dead (status 2)", status_of(guid) == HARDCORE_DEAD, status_of(guid))


# --- 6. drop ---

def part_drop():
    results.check("drop: the player drops the status", drop_hardcore(realm, player))
    rows = realm.sql("characters", "SELECT guid, status, dropped_at FROM character_hardcore WHERE guid IN (%d, %d, %d)"
                     % (low(player.guid), guids[tank], guids[healer]))
    table = {int(g): (s, d) for g, s, d in (line.split("\t") for line in rows.splitlines() if line)}
    mine = table.get(low(player.guid), ("", ""))
    for name in (tank, healer):
        bot = table.get(guids[name], ("", ""))
        results.check("drop: the %s dropped its status at the same moment" % role[name], bot[0] == HARDCORE_DROPPED
                      and bot[1] == mine[1], (mine, bot))
    results.check("drop: the dead ranged member stays dead", status_of(guids[ranged]) == HARDCORE_DEAD, status_of(guids[ranged]))


# --- 8. players ---

def new_player(account, name):
    client = realm.login(account, name, client_class=Client)
    client.gm(".gm off")
    return client


def part_players():
    # take: in a group with a character that is not hardcore.
    joiner = new_player("PLAYER257C", "Hcjoin" + tag)
    normal.group_invite("Hcjoin" + tag)
    joiner.group_accept(wait=2.0)
    grouped = in_group_with(normal.guid, joiner.guid)
    results.check("take: the new character is in a group with a character that is not hardcore", grouped)
    start = len(joiner.chat_log)
    results.check("take: the NPC refuses the status in that group", not take_hardcore(realm, joiner)
                  and status_of(joiner.guid) == "", status_of(joiner.guid))
    results.check("take: the refusal says to leave the group", said(joiner, start, TAKE_IN_GROUP), system_lines(joiner, start))
    joiner.group_leave(wait=2.0)
    normal.group_leave(wait=1.0)
    results.check("take: out of the group, it takes the status", take_hardcore(realm, joiner))

    # accept: an invite from before the status, accepted after it (the accept passes Group::AddMember only).
    pending = new_player("PLAYER257E", "Hcpend" + tag)
    normal.group_invite("Hcpend" + tag)
    results.check("accept: with an open invite (no group yet) a character takes the status", take_hardcore(realm, pending))
    start = len(pending.chat_log)
    pending.group_accept(wait=2.0)
    results.check("accept: the old invite of a character that is not hardcore is refused at the accept",
                  said(pending, start, NO_GROUP) and not in_group_with(normal.guid, pending.guid),
                  system_lines(pending, start))
    normal.group_leave(wait=1.0)
    try:
        pending.sock.close()
    except Exception:
        pass

    # drop: a group of two hardcore players.
    second = new_player("PLAYER257D", "Hcsecd" + tag)
    results.check("drop: a second hardcore player", take_hardcore(realm, second))
    joiner.group_invite("Hcsecd" + tag)
    second.group_accept(wait=2.0)
    results.check("drop: two hardcore players group", in_group_with(joiner.guid, second.guid))
    start = len(joiner.chat_log)
    results.check("drop: the NPC refuses the drop with another hardcore player in the group",
                  not drop_hardcore(realm, joiner) and status_of(joiner.guid) == HARDCORE_ALIVE and status_of(second.guid) == HARDCORE_ALIVE,
                  (status_of(joiner.guid), status_of(second.guid)))
    results.check("drop: the refusal names the other hardcore player", said(joiner, start, DROP_WITH_PLAYER),
                  system_lines(joiner, start))
    second.group_leave(wait=2.0)

    # dead invite: the second player dies alone; in its 60 seconds a /invite of it is refused.
    second.gm(".die", select=second.guid, wait=2.0)
    results.check("dead invite: the second player died for good", status_of(second.guid) == HARDCORE_DEAD, status_of(second.guid))
    start = len(normal.chat_log)
    normal.group_invite("Hcsecd" + tag)
    normal.pump(2)
    results.check("dead invite: a /invite of a dead hardcore character is refused", said(normal, start, DEAD_BOT)
                  and not in_group_with(normal.guid, second.guid), system_lines(normal, start))

    # player death: the first player, with a hardcore bot in its group, dies.
    answer = joiner.gm(".wowai bot create %s priest healer hardcore" % follower, wait=2.0)
    results.check("player death: the player makes a hardcore bot", any("Hardcore bot %s made" % follower in line
                  for line in answer), answer)
    realm.wait_log(r"\[Bots\] %s logs in" % follower, 20, joiner)
    joiner.chat(CHAT_MSG_WHISPER, "invite", target=follower)
    joiner.pump(4)
    bot = realm.guid_of(follower)
    results.check("player death: the hardcore bot joins", in_group_with(joiner.guid, bot))
    joiner.gm(".namego %s" % follower, wait=1.0)
    realm.mark("player death")
    joiner.gm(".die", select=joiner.guid, wait=2.0)
    results.check("player death: the player died for good", status_of(joiner.guid) == HARDCORE_DEAD, status_of(joiner.guid))
    try:
        joiner.wait_for(wowclient.SMSG_LOGOUT_COMPLETE, 80)
        logged_out = True
    except Exception as error:
        print("  player death logout:", error)
        logged_out = False
    results.check("player death: the server logs the dead player out", logged_out)
    gone = realm.wait_log(r"\[Bots\] %s is out of its group and logs out" % follower, 20)
    rows = realm.sql("characters", "SELECT COUNT(*) FROM group_member WHERE member_guid IN (%d, %d)"
                     % (low(joiner.guid), bot))
    results.check("player death: the dead player and its bot are out of the group", rows == "0", rows)
    results.check("player death: the bot logs out", gone and not online(bot))
    for client in (joiner, second):
        try:
            client.sock.close()
        except Exception:
            pass


# --- 7. restart ---

def part_restart():
    print(realm.run("restart"))
    time.sleep(5)
    results.check("restart: the dead ranged member is not logged in", not online(guids[ranged]))
    answer = soap(".wowai bot login %s" % ranged)
    results.check("restart: .wowai bot login still refuses it", DEAD_BOT in answer, answer)


realm = Realm()
results = Results()
tag = suffix()
tank, healer, ranged, over = ("Hctank" + tag, "Hcheal" + tag, "Hcrang" + tag, "Hcover" + tag)
climber, follower = "Hcclim" + tag, "Hcfoll" + tag
role = {tank: "the tank", healer: "the healer", ranged: "the ranged member"}
guids = {}
player = realm.login("PLAYER257A", "Hcplay" + tag, client_class=Client)
normal = realm.login("PLAYER257B", "Hcnorm" + tag, client_class=Client)
player.gm(".gm off")
for name, part in (("make", part_make), ("border", part_border), ("level", part_level), ("trade", part_trade),
                   ("loot", part_loot), ("death", part_death), ("drop", part_drop), ("players", part_players)):
    print("== part %s" % name)
    part()
for client in (player, normal):
    try:
        client.logout()
    except Exception:
        pass
    client.sock.close()
print("== part restart")
part_restart()
results.finish()
