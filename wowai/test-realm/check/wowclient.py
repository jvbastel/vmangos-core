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
SMSG_LOGIN_VERIFY_WORLD = 566
MSG_MOVE_START_FORWARD = 181
CMSG_SET_ACTIVE_MOVER = 618
MSG_MOVE_STOP = 183
MSG_MOVE_HEARTBEAT = 238
SMSG_MONSTER_MOVE = 221
MOVEFLAG_FORWARD = 0x1

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
