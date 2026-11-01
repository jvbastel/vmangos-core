#!/usr/bin/env python3
"""A minimal headless 1.12.1 (build 5875) world client for in-game checks.

It skips realmd: `realm.sh --ticket N key <account>` writes a session key into
the `account` row and prints it. Then this client connects to mangosd, logs
in a character (it makes one if the account has none), and runs the steps of
a check. kit.py does the key and the login for a check script.

    c = WorldClient("127.0.0.1", 21062, "PLAYER1", key_bytes(key_hex))
    c.login("Thrallson", race=2, cls=1)
    c.chat(CHAT_MSG_WHISPER, "follow", target="Grukk")
    c.gm(".gps", select=bot_guid)  # returns the system lines

The client prints each chat line of another unit as it arrives (`echo`),
so that the output of a check shows what the bots answer to each command.
It acks near and far teleports like a real client. It says and whispers in
the language of its character's race.
"""
import hashlib
import os
import socket
import struct
import sys
import time

SMSG_AUTH_CHALLENGE = 492
CMSG_AUTH_SESSION = 493
SMSG_AUTH_RESPONSE = 494
CMSG_CHAR_CREATE = 54
CMSG_CHAR_ENUM = 55
SMSG_CHAR_CREATE = 58
SMSG_CHAR_ENUM = 59
CMSG_PLAYER_LOGIN = 61
CMSG_MESSAGECHAT = 149
SMSG_MESSAGECHAT = 150
MSG_MOVE_TELEPORT_ACK = 199
SMSG_NEW_WORLD = 62
MSG_MOVE_WORLDPORT_ACK = 220
CMSG_SET_SELECTION = 317
CMSG_DESTROYITEM = 273
SMSG_LOGIN_VERIFY_WORLD = 566
MSG_MOVE_START_FORWARD = 181
CMSG_SET_ACTIVE_MOVER = 618
MSG_MOVE_STOP = 183
MSG_MOVE_HEARTBEAT = 238
SMSG_MONSTER_MOVE = 221
SMSG_TRIGGER_CINEMATIC = 250
CMSG_COMPLETE_CINEMATIC = 252
MOVEFLAG_FORWARD = 0x1
CMSG_LOGOUT_REQUEST = 75
SMSG_LOGOUT_COMPLETE = 77
CMSG_SET_ACTION_BUTTON = 296
SMSG_INITIAL_SPELLS = 298
SMSG_LEARNED_SPELL = 299
SMSG_SUPERCEDED_SPELL = 300
CMSG_CAST_SPELL = 302
SMSG_CAST_RESULT = 304
SMSG_SPELL_GO = 306
CMSG_LEARN_TALENT = 593
TARGET_FLAG_UNIT = 0x0002
CMSG_TRAINER_LIST, SMSG_TRAINER_LIST = 432, 433
CMSG_TRAINER_BUY_SPELL, SMSG_TRAINER_BUY_SUCCEEDED, SMSG_TRAINER_BUY_FAILED = 434, 435, 436
# One spell of SMSG_TRAINER_LIST (NPCHandler.cpp, SendTrainerSpellHelper): spell, state, cost, 2 x
# profession flags, level, required skill, required skill value, 2 x chain spells, 0.
TRAINER_SPELL = struct.Struct("<IBIIIBIIIII")
CMSG_GOSSIP_HELLO, CMSG_GOSSIP_SELECT_OPTION, SMSG_GOSSIP_MESSAGE, SMSG_GOSSIP_COMPLETE = 379, 380, 381, 382

CHAT_MSG_SAY = 0x00
CHAT_MSG_PARTY = 0x01
CHAT_MSG_WHISPER = 0x06
CHAT_MSG_RAID = 0x02
CHAT_MSG_GUILD = 0x03
CHAT_MSG_YELL = 0x05
CHAT_MSG_SYSTEM = 0x0A
CHAT_MSG_MONSTER_SAY = 0x0B
CHAT_MSG_MONSTER_YELL = 0x0C
CHAT_MSG_MONSTER_EMOTE = 0x0D
CHAT_MSG_CHANNEL = 0x0E
CHAT_MSG_MONSTER_WHISPER = 0x1A
CHAT_NAMES = {0x00: "say", 0x01: "party", 0x02: "raid", 0x03: "guild", 0x05: "yell", 0x06: "whisper",
              0x08: "emote", 0x0B: "monster say", 0x0C: "monster yell", 0x0D: "monster emote",
              0x0E: "channel", 0x1A: "monster whisper"}
LANG_UNIVERSAL = 0
LANG_ORCISH = 1
LANG_COMMON = 7
HORDE_RACES = (2, 5, 6, 8)  # orc, undead, tauren, troll


def language_of_race(race):
    """The language that a character of the race says and whispers in. mangosd
    refuses LANG_UNIVERSAL for a say, and a language that the race does not know."""
    return LANG_ORCISH if race in HORDE_RACES else LANG_COMMON


def key_bytes(key_hex):
    """The session key for the client, from the hex of `realm.sh key`."""
    return bytes(reversed(bytes.fromhex(key_hex)))


def new_session_key():
    """40 random bytes (little-endian, top byte non-zero), and the hex that
    BigNumber::SetHexStr reads (big-endian)."""
    k = bytearray(os.urandom(40))
    k[-1] |= 0x80
    return bytes(k), bytes(reversed(k)).hex().upper()


class Crypt:
    def __init__(self, key):
        self.key = key
        self.si = self.sj = self.ri = self.rj = 0

    def encrypt(self, data):
        out = bytearray(data)
        for t in range(len(out)):
            self.si %= len(self.key)
            x = ((out[t] ^ self.key[self.si]) + self.sj) & 0xFF
            self.si += 1
            out[t] = self.sj = x
        return bytes(out)

    def decrypt(self, data):
        out = bytearray(data)
        for t in range(len(out)):
            self.ri %= len(self.key)
            x = ((out[t] - self.rj) & 0xFF) ^ self.key[self.ri]
            self.ri += 1
            self.rj = out[t]
            out[t] = x
        return bytes(out)


def cstr(data, pos):
    end = data.index(b"\0", pos)
    return data[pos:end].decode("utf-8", "replace"), end + 1


class WorldClient:
    """names: guid -> name, for the echo. echo: where the chat of other units
    goes (None: nowhere). sock: a connected socket instead of host and port."""

    def __init__(self, host, port, account, key, names=None, echo=sys.stdout, sock=None):
        self.account = account.upper()
        self.key = key
        self.sock = sock or socket.create_connection((host, port), timeout=10)
        self.crypt = None
        self.guid = 0
        self.race = 2
        self.names = dict(names or {})
        self.echo = echo
        self.chat_log = []  # (type, sender guid, text)
        self.move_log = []  # (time.time(), unit guid, spline type, duration ms): SMSG_MONSTER_MOVE, type 1 = stop

    # --- framing ---
    def _recv_exact(self, n):
        buf = b""
        while len(buf) < n:
            part = self.sock.recv(n - len(buf))
            if not part:
                raise ConnectionError("closed")
            buf += part
        return buf

    def recv(self, timeout=10):
        self.sock.settimeout(timeout)
        head = self._recv_exact(4)
        if self.crypt:
            head = self.crypt.decrypt(head)
        size, opcode = struct.unpack(">H", head[:2])[0], struct.unpack("<H", head[2:])[0]
        body = self._recv_exact(size - 2)
        self._observe(opcode, body)
        return opcode, body

    def send(self, opcode, body=b""):
        head = struct.pack(">H", len(body) + 4) + struct.pack("<I", opcode)
        if self.crypt:
            head = self.crypt.encrypt(head)
        self.sock.sendall(head + body)

    def wait_for(self, opcode, timeout=15):
        end = time.time() + timeout
        while time.time() < end:
            op, body = self.recv(max(0.1, end - time.time()))
            if op == opcode:
                return body
        raise TimeoutError("no opcode %d" % opcode)

    def pump(self, seconds):
        end = time.time() + seconds
        while time.time() < end:
            try:
                self.recv(max(0.05, end - time.time()))
            except socket.timeout:
                break

    def _observe(self, opcode, body):
        if opcode == SMSG_MESSAGECHAT:
            line = parse_chat(body)
            self.chat_log.append(line)
            self._echo(*line)
        elif opcode == SMSG_MONSTER_MOVE:
            guid, pos = unpack_guid(body, 0)
            # Position (3 floats), spline id, the type, its facing data,
            # then flags and duration. A pure turn has duration 0.
            kind = body[pos + 16]
            duration = 0
            if kind != 1:
                at = pos + 17 + {0: 0, 2: 12, 3: 8, 4: 4}.get(kind, 0)
                duration = struct.unpack_from("<I", body, at + 4)[0]
            self.move_log.append((time.time(), guid, kind, duration))
        elif opcode == SMSG_TRIGGER_CINEMATIC:
            # The intro of a new character: skip it like a player who presses
            # Escape. Until then the server summons a camera creature for the
            # character each second, which takes its summon slots (WOWAI#90).
            self.send(CMSG_COMPLETE_CINEMATIC)
        elif opcode == SMSG_NEW_WORLD:
            # Far teleport: ack it like a client, then the server adds us to the new map.
            self.send(MSG_MOVE_WORLDPORT_ACK)
        elif opcode == MSG_MOVE_TELEPORT_ACK:
            # Near teleport: ack it like a client. Packed guid, counter, ...
            guid = self.guid
            counter = 0
            try:
                mask = body[0]
                pos = 1 + bin(mask).count("1")
                counter = struct.unpack_from("<I", body, pos)[0]
            except Exception:
                pass
            self.send(MSG_MOVE_TELEPORT_ACK, struct.pack("<QII", guid, counter, int(time.time() * 1000) & 0xFFFFFFFF))

    def _echo(self, chat_type, sender, text):
        if self.echo is None or chat_type == CHAT_MSG_SYSTEM or (sender and sender == self.guid):
            return
        name = self.names.get(sender, "0x%x" % sender)
        print("  chat %s %s: %s" % (CHAT_NAMES.get(chat_type, "type %d" % chat_type), name, text), file=self.echo)
        self.echo.flush()

    # --- login ---
    def login(self, name, race=2, cls=1):
        body = self.wait_for(SMSG_AUTH_CHALLENGE)
        server_seed = struct.unpack("<I", body[:4])[0]
        client_seed = struct.unpack("<I", os.urandom(4))[0]
        digest = hashlib.sha1(self.account.encode() + struct.pack("<III", 0, client_seed, server_seed) + self.key).digest()
        self.send(CMSG_AUTH_SESSION, struct.pack("<II", 5875, 0) + self.account.encode() + b"\0" +
                  struct.pack("<I", client_seed) + digest + struct.pack("<I", 0))
        self.crypt = Crypt(self.key)
        body = self.wait_for(SMSG_AUTH_RESPONSE)
        if body[0] != 0x0C:
            raise RuntimeError("auth response %d" % body[0])

        self.guid, self.race = self._find_char(name)
        if not self.guid:
            self.send(CMSG_CHAR_CREATE, name.encode() + b"\0" + bytes([race, cls, 0, 0, 0, 0, 0, 0, 0]))
            code = self.wait_for(SMSG_CHAR_CREATE)[0]
            self.guid, self.race = self._find_char(name)
            if not self.guid:
                raise RuntimeError("char create %d" % code)
        self.names.setdefault(self.guid, name)
        self.send(CMSG_PLAYER_LOGIN, struct.pack("<Q", self.guid))
        self.wait_for(SMSG_LOGIN_VERIFY_WORLD, 30)
        self.pump(2)

    def logout(self):
        """Logout to the character screen (the server saves the character)."""
        self.send(CMSG_LOGOUT_REQUEST)
        self.wait_for(SMSG_LOGOUT_COMPLETE, 30)
        self.pump(2)

    def login_again(self):
        """Logs the same character in again, after logout()."""
        self.send(CMSG_PLAYER_LOGIN, struct.pack("<Q", self.guid))
        self.wait_for(SMSG_LOGIN_VERIFY_WORLD, 30)
        self.pump(3)

    def relog(self):
        self.logout()
        self.login_again()

    def _find_char(self, name):
        """(guid, race) of the character of the account with the name, or (0, 0)."""
        self.send(CMSG_CHAR_ENUM)
        body = self.wait_for(SMSG_CHAR_ENUM)
        count, pos = body[0], 1
        for _ in range(count):
            guid = struct.unpack_from("<Q", body, pos)[0]
            cname, after = cstr(body, pos + 8)
            if cname.lower() == name.lower():
                return guid, body[after]
            # Skip the rest of the record: name, 9 bytes race..facialhair, zone u32, map u32,
            # xyz, guild u32, flags u32, first login u8, pet 3x u32, 20 items x 5 bytes.
            _, pos = cstr(body, pos + 8)
            pos += 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 4 + 4 + 12 + 4 + 4 + 1 + 12 + 20 * 5
        return 0, 0

    # --- actions ---
    def chat(self, chat_type, text, target=None, lang=None):
        """Say, party, or whisper (target: the name) a line. A GM command is a say."""
        if lang is None:
            lang = language_of_race(self.race)
        body = struct.pack("<II", chat_type, lang)
        if target:
            body += target.encode() + b"\0"
        body += text.encode() + b"\0"
        self.send(CMSG_MESSAGECHAT, body)

    def set_active_mover(self):
        """CMSG_SET_ACTIVE_MOVER for the own character, as a client sends after login: else the
        server ignores the movement packets of this client."""
        self.send(CMSG_SET_ACTIVE_MOVER, struct.pack("<Q", self.guid))

    def move(self, opcode, x, y, z, o, flags=0):
        """A client movement packet (1.12 MovementInfo, no transport)."""
        body = struct.pack("<II4fI", flags, int(time.time() * 1000) & 0xFFFFFFFF, x, y, z, o, 0)
        self.send(opcode, body)

    def select(self, guid):
        self.send(CMSG_SET_SELECTION, struct.pack("<Q", guid))

    def gm(self, command, select=None, wait=1.5):
        if select is not None:
            # The selection and the chat command run on different packet
            # queues: wait so that the command sees the new selection.
            self.select(select)
            self.pump(0.5)
        start = len(self.chat_log)
        self.chat(CHAT_MSG_SAY, command)
        self.pump(wait)
        return [text for (t, _, text) in self.chat_log[start:] if t == CHAT_MSG_SYSTEM]


def pack_guid(guid):
    """A packed guid, as the client writes a unit target."""
    mask, data = 0, b""
    for i in range(8):
        byte = (guid >> (8 * i)) & 0xFF
        if byte:
            mask |= 1 << i
            data += bytes([byte])
    return bytes([mask]) + data


class SpellClient(WorldClient):
    """A WorldClient that keeps the spell packets of its character, for the checks of spells and talents:
    learned (SMSG_LEARNED_SPELL), initial_spells (the spellbook of the last login, a set), superseded
    ((old, new) of SMSG_SUPERCEDED_SPELL), spell_go (its own SMSG_SPELL_GO spells) and cast_results
    ((spell, fail reason or None) of SMSG_CAST_RESULT). A check that reads more packets subclasses it and
    calls super()._observe."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.learned = []
        self.initial_spells = None
        self.superseded = []
        self.spell_go = []
        self.cast_results = []

    def _observe(self, opcode, body):
        super()._observe(opcode, body)
        try:
            if opcode == SMSG_LEARNED_SPELL:
                self.learned.append(struct.unpack_from("<H", body, 0)[0])
            elif opcode == SMSG_INITIAL_SPELLS:
                # uint8 0, uint16 count, then count x (uint16 spell, int16 0), then the cooldowns.
                count = struct.unpack_from("<H", body, 1)[0]
                self.initial_spells = {struct.unpack_from("<H", body, 3 + 4 * i)[0] for i in range(count)}
            elif opcode == SMSG_SUPERCEDED_SPELL:
                self.superseded.append(struct.unpack_from("<HH", body, 0))
            elif opcode == SMSG_SPELL_GO:
                # packed item or caster guid, packed caster guid, uint32 spell, ...
                _, pos = unpack_guid(body, 0)
                caster, pos = unpack_guid(body, pos)
                if caster == self.guid:
                    self.spell_go.append(struct.unpack_from("<I", body, pos)[0])
            elif opcode == SMSG_CAST_RESULT:
                # uint32 spell, uint8 status (2 = failed), uint8 reason when it failed.
                spell, status = struct.unpack_from("<IB", body, 0)
                self.cast_results.append((spell, body[5] if status == 2 and len(body) > 5 else None))
        except (IndexError, struct.error):
            pass

    def knows(self, spell):
        """The spell is in the spellbook: at the last login, or learned since."""
        return spell in (self.initial_spells or set()) or spell in self.learned

    def login_again(self):
        self.initial_spells = None
        super().login_again()

    def learn_talent(self, talent, rank=0, wait=1.5):
        """The spells that the server teaches for one CMSG_LEARN_TALENT (rank from 0)."""
        start = len(self.learned)
        self.send(CMSG_LEARN_TALENT, struct.pack("<II", talent, rank))
        self.pump(wait)
        return self.learned[start:]

    def learn_ranks(self, talent, ranks):
        """Learns ranks 1 to `ranks` of a talent; the spells taught."""
        return [spell for rank in range(ranks) for spell in self.learn_talent(talent, rank)]

    def set_button(self, button, spell, wait=1):
        self.send(CMSG_SET_ACTION_BUTTON, struct.pack("<BI", button, spell))
        self.pump(wait)

    def cast_at(self, spell, target=None):
        """Sends CMSG_CAST_SPELL, as the action bar does: on itself, or at the unit `target`. Does not wait."""
        targets = struct.pack("<H", 0) if target is None else struct.pack("<H", TARGET_FLAG_UNIT) + pack_guid(target)
        self.send(CMSG_CAST_SPELL, struct.pack("<I", spell) + targets)

    def cast(self, spell, target=None, wait=2):
        """Casts and waits: (the fail reasons of its cast results, whether a SMSG_SPELL_GO of it came)."""
        start_results, start_go = len(self.cast_results), len(self.spell_go)
        self.cast_at(spell, target)
        self.pump(wait)
        reasons = [reason for s, reason in self.cast_results[start_results:] if s == spell]
        return reasons, spell in self.spell_go[start_go:]

    def cast_went(self, spell, target=None, wait=2):
        """Casts and waits; True when a SMSG_SPELL_GO of the spell came."""
        return self.cast(spell, target, wait)[1]


class TrainerClient(SpellClient):
    """A SpellClient that keeps the last SMSG_TRAINER_LIST, trainer_spells: {spell: (state, required skill,
    skill value)}, and the answers to its trainer buys, trainer_buys: [(spell, True or the fail reason)].
    The states (TrainerSpellState): 0 green (can learn), 1 red (cannot yet), 2 grey (known)."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.trainer_spells = None
        self.trainer_buys = []

    def _observe(self, opcode, body):
        super()._observe(opcode, body)
        try:
            if opcode == SMSG_TRAINER_LIST and len(body) >= 16:
                count = struct.unpack_from("<I", body, 12)[0]
                spells = {}
                for k in range(count):
                    if 16 + (k + 1) * TRAINER_SPELL.size > len(body):
                        break
                    spell, state, _, _, _, _, skill, value, _, _, _ = TRAINER_SPELL.unpack_from(body, 16 + k * TRAINER_SPELL.size)
                    spells[spell] = (state, skill, value)
                self.trainer_spells = spells
            elif opcode == SMSG_TRAINER_BUY_SUCCEEDED:
                # uint64 trainer guid, uint32 spell.
                self.trainer_buys.append((struct.unpack_from("<I", body, 8)[0], True))
            elif opcode == SMSG_TRAINER_BUY_FAILED:
                # uint64 trainer guid, uint32 spell, uint32 reason.
                self.trainer_buys.append(struct.unpack_from("<II", body, 8))
        except (IndexError, struct.error):
            pass

    def trainer_list(self, guid, wait=1.5):
        """The spells that the trainer `guid` offers, as its trainer window shows them, or None."""
        self.trainer_spells = None
        self.send(CMSG_TRAINER_LIST, struct.pack("<Q", guid))
        self.pump(wait)
        return self.trainer_spells

    def buy_spell(self, guid, spell, wait=1.5):
        """Buys the trainer spell `spell` of the trainer `guid`, as the trainer window's Train button does. The
        answers of the server: [True or the fail reason]."""
        start = len(self.trainer_buys)
        self.send(CMSG_TRAINER_BUY_SPELL, struct.pack("<QI", guid, spell))
        self.pump(wait)
        return [answer for s, answer in self.trainer_buys[start:] if s == spell]


class GossipMenu:
    """One SMSG_GOSSIP_MESSAGE: the npc guid, the npc_text id of its text, and its options as (index, icon,
    coded, text). A coded option asks the client for a code (a text box)."""

    def __init__(self, npc, text_id, options):
        self.npc = npc
        self.text_id = text_id
        self.options = options

    def option(self, words):
        """The index of the first option whose text holds `words` (no case), or None."""
        for index, _, _, text in self.options:
            if words.lower() in text.lower():
                return index
        return None

    def __repr__(self):
        return "GossipMenu(text %d, %s)" % (self.text_id, [text for _, _, _, text in self.options])


class GossipClient(WorldClient):
    """A WorldClient that talks to gossip NPCs: gossip_hello opens the menu of an NPC, gossip_select picks an
    option. gossip is the last menu that the server sent, or None after SMSG_GOSSIP_COMPLETE (the menu
    closed). It calls super()._observe, so a check can mix it with another client class."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.gossip = None

    def _observe(self, opcode, body):
        super()._observe(opcode, body)
        try:
            if opcode == SMSG_GOSSIP_MESSAGE:
                # PlayerMenu::SendGossipMenu (1.12): npc guid, text id, option count, then per option the
                # index (u32), icon (u8), coded (u8) and text. The quest list after it is skipped.
                npc, text_id, count = struct.unpack_from("<QII", body, 0)
                pos, options = 16, []
                for _ in range(count):
                    index, icon, coded = struct.unpack_from("<IBB", body, pos)
                    text, pos = cstr(body, pos + 6)
                    options.append((index, icon, bool(coded), text))
                self.gossip = GossipMenu(npc, text_id, options)
            elif opcode == SMSG_GOSSIP_COMPLETE:
                self.gossip = None
        except (IndexError, struct.error):
            pass

    def gossip_hello(self, npc, wait=1.5):
        """Opens the gossip of the NPC `npc` (a full guid), as a right click does. Its menu, or None when the
        NPC sends none (out of reach, or no gossip)."""
        self.gossip = None
        self.send(CMSG_GOSSIP_HELLO, struct.pack("<Q", npc))
        self.pump(wait)
        return self.gossip

    def gossip_select(self, npc, index, code=None, wait=1.5):
        """Picks option `index` of the open menu of `npc` (with `code` for a coded option). The next menu, or
        None when the NPC closes the gossip."""
        self.gossip = None
        body = struct.pack("<QI", npc, index)
        if code is not None:
            body += code.encode() + b"\0"
        self.send(CMSG_GOSSIP_SELECT_OPTION, body)
        self.pump(wait)
        return self.gossip


# Trade, mail, auction house, group and guild (WOWAI#254). Opcodes of 1.12.1 (Opcodes_1_12_1.h).
CMSG_GROUP_INVITE = 110
SMSG_GROUP_INVITE = 111
CMSG_GROUP_ACCEPT = 114
CMSG_GROUP_DECLINE = 115
CMSG_GROUP_DISBAND = 123  # leave the group
SMSG_GROUP_LIST = 125
SMSG_PARTY_COMMAND_RESULT = 127
CMSG_GUILD_INVITE = 130
SMSG_GUILD_INVITE = 131
CMSG_GUILD_ACCEPT = 132
CMSG_GUILD_DECLINE = 133
SMSG_GUILD_EVENT = 146
SMSG_GUILD_COMMAND_RESULT = 147
CMSG_INITIATE_TRADE = 278
CMSG_BEGIN_TRADE = 279
CMSG_ACCEPT_TRADE = 282
CMSG_CANCEL_TRADE = 284
CMSG_SET_TRADE_GOLD = 287
SMSG_TRADE_STATUS = 288
CMSG_SEND_MAIL = 568
SMSG_SEND_MAIL_RESULT = 569
CMSG_MAIL_TAKE_MONEY = 581
CMSG_MAIL_TAKE_ITEM = 582
MSG_AUCTION_HELLO = 597
SMSG_RECEIVED_MAIL = 645

# TradeStatus (SharedDefines.h).
TRADE_STATUS_BUSY = 0
TRADE_STATUS_BEGIN_TRADE = 1
TRADE_STATUS_OPEN_WINDOW = 2
TRADE_STATUS_TRADE_CANCELED = 3
TRADE_STATUS_TRADE_ACCEPT = 4
TRADE_STATUS_TRADE_COMPLETE = 8
# MailResponseType and MailResponseResult (SharedDefines.h).
MAIL_SEND = 0
MAIL_MONEY_TAKEN = 1
MAIL_ITEM_TAKEN = 2
MAIL_OK = 0
MAIL_ERR_INTERNAL_ERROR = 6
MAIL_STATIONERY_DEFAULT = 41
# PartyOperation and PartyResult (WorldSession.h).
PARTY_OP_INVITE = 0
ERR_PARTY_RESULT_OK = 0
# GuildEvents (Guild.h).
GE_JOINED = 3


class TradeClient(WorldClient):
    """A WorldClient that trades with another character. trade_statuses: each SMSG_TRADE_STATUS status, in order;
    trade_asked_by: the character that last asked this one to trade (TRADE_STATUS_BEGIN_TRADE). A trade: one
    side calls trade_initiate(other), the other side trade_begin() (both get TRADE_STATUS_OPEN_WINDOW), then
    trade_gold and trade_accept on both sides (TRADE_STATUS_TRADE_COMPLETE). Mixable (super()._observe)."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.trade_statuses = []
        self.trade_asked_by = 0

    def _observe(self, opcode, body):
        super()._observe(opcode, body)
        if opcode == SMSG_TRADE_STATUS and len(body) >= 4:
            status = struct.unpack_from("<I", body, 0)[0]
            self.trade_statuses.append(status)
            if status == TRADE_STATUS_BEGIN_TRADE and len(body) >= 12:
                self.trade_asked_by = struct.unpack_from("<Q", body, 4)[0]

    def _trade_send(self, opcode, body=b"", wait=1.5):
        start = len(self.trade_statuses)
        self.send(opcode, body)
        self.pump(wait)
        return self.trade_statuses[start:]

    def trade_initiate(self, other, wait=1.5):
        """Asks the character `other` (a full guid) to trade. The trade statuses that came meanwhile."""
        return self._trade_send(CMSG_INITIATE_TRADE, struct.pack("<Q", other), wait)

    def trade_begin(self, wait=1.5):
        """Says yes to the trade that another character asked for."""
        return self._trade_send(CMSG_BEGIN_TRADE, b"", wait)

    def trade_gold(self, copper, wait=1.0):
        return self._trade_send(CMSG_SET_TRADE_GOLD, struct.pack("<I", copper), wait)

    def trade_accept(self, wait=1.5):
        return self._trade_send(CMSG_ACCEPT_TRADE, struct.pack("<I", 1), wait)

    def trade_cancel(self, wait=1.0):
        return self._trade_send(CMSG_CANCEL_TRADE, b"", wait)


class MailClient(WorldClient):
    """A WorldClient that sends mail and takes what a mail holds. The character must stand at a mailbox (its full
    guid). mail_results: each SMSG_SEND_MAIL_RESULT as (mail id, action, error); new_mail: the count of
    SMSG_RECEIVED_MAIL notes (a mail arrived). Mixable (super()._observe)."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.mail_results = []
        self.new_mail = 0

    def _observe(self, opcode, body):
        super()._observe(opcode, body)
        if opcode == SMSG_SEND_MAIL_RESULT and len(body) >= 12:
            self.mail_results.append(struct.unpack_from("<III", body, 0))
        elif opcode == SMSG_RECEIVED_MAIL:
            self.new_mail += 1

    def _mail_send(self, opcode, body, wait):
        start = len(self.mail_results)
        self.send(opcode, body)
        self.pump(wait)
        return self.mail_results[start:]

    def send_mail(self, mailbox, receiver, subject, text, item=0, money=0, cod=0, wait=2.0):
        """Sends a mail to the character named `receiver`, with an item (its full guid), money and cash on
        delivery (copper). The mail results that came meanwhile: [(0, MAIL_SEND, MAIL_OK)] when it is sent."""
        body = (struct.pack("<Q", mailbox) + receiver.encode() + b"\0" + subject.encode() + b"\0" + text.encode()
                + b"\0" + struct.pack("<II", MAIL_STATIONERY_DEFAULT, 0) + struct.pack("<QII", item, money, cod)
                + struct.pack("<QB", 0, 0))
        return self._mail_send(CMSG_SEND_MAIL, body, wait)

    def mail_take_item(self, mailbox, mail_id, wait=1.5):
        """Takes the item of the mail `mail_id` (the id column of the mail table)."""
        return self._mail_send(CMSG_MAIL_TAKE_ITEM, struct.pack("<QI", mailbox, mail_id), wait)

    def mail_take_money(self, mailbox, mail_id, wait=1.5):
        return self._mail_send(CMSG_MAIL_TAKE_MONEY, struct.pack("<QI", mailbox, mail_id), wait)


class AuctionClient(WorldClient):
    """A WorldClient that opens the auction house. auction_hello returns (auctioneer, house id) when the
    server opens the window, else None. Mixable (super()._observe)."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.auction_house = None

    def _observe(self, opcode, body):
        super()._observe(opcode, body)
        if opcode == MSG_AUCTION_HELLO and len(body) >= 12:
            self.auction_house = struct.unpack_from("<QI", body, 0)

    def auction_hello(self, auctioneer, wait=1.5):
        """Talks to the auctioneer `auctioneer` (a full guid), as a right click does."""
        self.auction_house = None
        self.send(MSG_AUCTION_HELLO, struct.pack("<Q", auctioneer))
        self.pump(wait)
        return self.auction_house


class GroupClient(WorldClient):
    """A WorldClient that invites to a group and answers invites. party_results: each SMSG_PARTY_COMMAND_RESULT
    as (operation, name, result); group_invites: the names of the characters that invited this one;
    group_members: guid -> name of the other members (SMSG_GROUP_LIST; empty without a group);
    group_leader: the leader's guid. Mixable (super()._observe)."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.party_results = []
        self.group_invites = []
        self.group_members = {}
        self.group_leader = 0

    def _observe(self, opcode, body):
        super()._observe(opcode, body)
        try:
            if opcode == SMSG_PARTY_COMMAND_RESULT:
                operation = struct.unpack_from("<I", body, 0)[0]
                name, pos = cstr(body, 4)
                self.party_results.append((operation, name, struct.unpack_from("<I", body, pos)[0]))
            elif opcode == SMSG_GROUP_INVITE:
                self.group_invites.append(cstr(body, 0)[0])
            elif opcode == SMSG_GROUP_LIST:
                # GroupList::AppendBodyTo: type, own flags, count, (name, guid, online, flags) each, leader.
                count, pos, members = struct.unpack_from("<I", body, 2)[0], 6, {}
                for _ in range(count):
                    name, pos = cstr(body, pos)
                    members[struct.unpack_from("<Q", body, pos)[0]] = name
                    pos += 10
                self.group_members = members
                self.group_leader = struct.unpack_from("<Q", body, pos)[0]
        except (IndexError, ValueError, struct.error):
            pass

    def group_invite(self, name, wait=1.5):
        """Invites the character `name`. The party results that came meanwhile."""
        start = len(self.party_results)
        self.send(CMSG_GROUP_INVITE, name.encode() + b"\0")
        self.pump(wait)
        return self.party_results[start:]

    def group_accept(self, wait=1.5):
        self.send(CMSG_GROUP_ACCEPT)
        self.pump(wait)

    def group_decline(self, wait=1.0):
        self.send(CMSG_GROUP_DECLINE)
        self.pump(wait)

    def group_leave(self, wait=1.0):
        self.send(CMSG_GROUP_DISBAND)
        self.pump(wait)


class GuildClient(WorldClient):
    """A WorldClient that invites to a guild and answers invites. guild_results: each SMSG_GUILD_COMMAND_RESULT
    as (command, name, result); guild_invites: (inviter, guild name) of each invite; guild_events: (event,
    strings) of each SMSG_GUILD_EVENT (GE_JOINED: the name of the new member). Mixable (super()._observe)."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.guild_results = []
        self.guild_invites = []
        self.guild_events = []

    def _observe(self, opcode, body):
        super()._observe(opcode, body)
        try:
            if opcode == SMSG_GUILD_COMMAND_RESULT:
                command = struct.unpack_from("<I", body, 0)[0]
                name, pos = cstr(body, 4)
                self.guild_results.append((command, name, struct.unpack_from("<I", body, pos)[0]))
            elif opcode == SMSG_GUILD_INVITE:
                inviter, pos = cstr(body, 0)
                self.guild_invites.append((inviter, cstr(body, pos)[0]))
            elif opcode == SMSG_GUILD_EVENT:
                event, count = body[0], body[1]
                pos, strings = 2, []
                for _ in range(count):
                    text, pos = cstr(body, pos)
                    strings.append(text)
                self.guild_events.append((event, strings))
        except (IndexError, ValueError, struct.error):
            pass

    def guild_invite(self, name, wait=1.5):
        """Invites the character `name` to the own guild. The guild results that came meanwhile."""
        start = len(self.guild_results)
        self.send(CMSG_GUILD_INVITE, name.encode() + b"\0")
        self.pump(wait)
        return self.guild_results[start:]

    def guild_accept(self, wait=1.5):
        self.send(CMSG_GUILD_ACCEPT)
        self.pump(wait)

    def guild_decline(self, wait=1.0):
        self.send(CMSG_GUILD_DECLINE)
        self.pump(wait)


def unpack_guid(data, pos):
    """A packed guid at pos: (guid, position after it)."""
    mask = data[pos]
    pos += 1
    guid = 0
    for i in range(8):
        if mask & (1 << i):
            guid |= data[pos] << (8 * i)
            pos += 1
    return guid, pos


def parse_chat(body):
    """(type, sender guid, text) of an SMSG_MESSAGECHAT of 1.12, in the layouts
    of ChatHandler::BuildChatPacket. A monster whisper or emote has no sender
    guid (0)."""
    chat_type = body[0]
    pos = 1 + 4  # type, language
    sender = 0
    if chat_type in (CHAT_MSG_MONSTER_WHISPER, CHAT_MSG_MONSTER_EMOTE):
        name_length = struct.unpack_from("<I", body, pos)[0]
        pos += 4 + name_length + 8  # name, target guid
    elif chat_type in (CHAT_MSG_MONSTER_SAY, CHAT_MSG_MONSTER_YELL):
        sender = struct.unpack_from("<Q", body, pos)[0]
        name_length = struct.unpack_from("<I", body, pos + 8)[0]
        pos += 8 + 4 + name_length + 8  # guid, name, target guid
    elif chat_type == CHAT_MSG_CHANNEL:
        _, pos = cstr(body, pos)  # channel name
        sender = struct.unpack_from("<Q", body, pos + 4)[0]  # after the player rank
        pos += 4 + 8
    else:
        sender = struct.unpack_from("<Q", body, pos)[0]
        pos += 8
        if chat_type in (CHAT_MSG_SAY, CHAT_MSG_PARTY, CHAT_MSG_YELL):  # the guid twice
            pos += 8
    length = struct.unpack_from("<I", body, pos)[0]
    text = body[pos + 4:pos + 4 + length].rstrip(b"\0").decode("utf-8", "replace")
    return chat_type, sender, text
