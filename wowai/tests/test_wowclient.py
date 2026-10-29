#!/usr/bin/env python3
"""Tests of the headless check client (wowai/test-realm/check/wowclient.py).

No realm: the packets are built here the way mangosd builds them
(ChatHandler::BuildChatPacket), and the client talks to one end of a
socket pair.

    python3 wowai/tests/test_wowclient.py
"""
import io
import os
import socket
import struct
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "test-realm", "check"))
import wowclient  # noqa: E402

GRUKK = 0x0000000000000011


def chat_packet(chat_type, text, sender=0, language=0, sender_name=None, channel=None):
    """An SMSG_MESSAGECHAT body, as ChatHandler::BuildChatPacket writes it."""
    body = struct.pack("<BI", chat_type, language)
    if chat_type in (0x00, 0x01, 0x05):  # say, party, yell
        body += struct.pack("<QQ", sender, sender)
    elif chat_type in (0x0B, 0x0C):  # monster say, monster yell
        name = sender_name.encode() + b"\0"
        body += struct.pack("<Q", sender) + struct.pack("<I", len(name)) + name + struct.pack("<Q", 0)
    elif chat_type == 0x0E:  # channel
        body += channel.encode() + b"\0" + struct.pack("<IQ", 0, sender)
    else:
        body += struct.pack("<Q", sender)
    message = text.encode() + b"\0"
    return body + struct.pack("<I", len(message)) + message + b"\0"


def packets(data):
    """The (opcode, body) pairs that a client sent, unencrypted."""
    out = []
    while data:
        size = struct.unpack(">H", data[:2])[0]
        opcode = struct.unpack("<I", data[2:6])[0]
        out.append((opcode, data[6:2 + size]))
        data = data[2 + size:]
    return out


def client(test, **kwargs):
    c = Client(**kwargs)
    test.addCleanup(c.close)
    return c


class Client:
    """A WorldClient on one end of a socket pair. `server` is the other end."""

    def __init__(self, client_class=wowclient.WorldClient, **kwargs):
        self.server, sock = socket.socketpair()
        self.echo = io.StringIO()
        self.client = client_class(None, None, "player1", b"k" * 40, sock=sock, echo=self.echo, **kwargs)

    def close(self):
        self.server.close()
        self.client.sock.close()

    def sent(self):
        self.server.settimeout(0.5)
        data = b""
        try:
            while True:
                part = self.server.recv(65536)
                if not part:
                    break
                data += part
        except socket.timeout:
            pass
        return packets(data)


class ParseChatTest(unittest.TestCase):
    def test_party_line_of_a_bot(self):
        body = chat_packet(wowclient.CHAT_MSG_PARTY, "I follow you.", sender=GRUKK, language=1)
        self.assertEqual(wowclient.parse_chat(body), (wowclient.CHAT_MSG_PARTY, GRUKK, "I follow you."))

    def test_whisper_of_a_bot(self):
        body = chat_packet(wowclient.CHAT_MSG_WHISPER, "No.", sender=GRUKK)
        self.assertEqual(wowclient.parse_chat(body), (wowclient.CHAT_MSG_WHISPER, GRUKK, "No."))

    def test_system_line(self):
        body = chat_packet(wowclient.CHAT_MSG_SYSTEM, "You are now in GM mode.")
        self.assertEqual(wowclient.parse_chat(body), (wowclient.CHAT_MSG_SYSTEM, 0, "You are now in GM mode."))

    def test_monster_say_has_the_name_before_the_text(self):
        body = chat_packet(wowclient.CHAT_MSG_MONSTER_SAY, "You will die!", sender=0xF130000000000042,
                           sender_name="Defias Pillager")
        self.assertEqual(wowclient.parse_chat(body), (wowclient.CHAT_MSG_MONSTER_SAY, 0xF130000000000042, "You will die!"))

    def test_channel_line(self):
        body = chat_packet(wowclient.CHAT_MSG_CHANNEL, "LFG", sender=GRUKK, channel="General")
        self.assertEqual(wowclient.parse_chat(body), (wowclient.CHAT_MSG_CHANNEL, GRUKK, "LFG"))


class LanguageTest(unittest.TestCase):
    """mangosd refuses a say in a language that the character does not know,
    and LANG_UNIVERSAL is not allowed for a say either."""

    def test_horde_races_speak_orcish(self):
        for race in (2, 5, 6, 8):  # orc, undead, tauren, troll
            self.assertEqual(wowclient.language_of_race(race), wowclient.LANG_ORCISH)

    def test_alliance_races_speak_common(self):
        for race in (1, 3, 4, 7):  # human, dwarf, night elf, gnome
            self.assertEqual(wowclient.language_of_race(race), wowclient.LANG_COMMON)

    def test_chat_uses_the_language_of_the_character(self):
        c = client(self)
        c.client.race = 1
        c.client.chat(wowclient.CHAT_MSG_SAY, ".gps")
        (opcode, body), = c.sent()
        self.assertEqual(opcode, wowclient.CMSG_MESSAGECHAT)
        self.assertEqual(struct.unpack_from("<II", body), (wowclient.CHAT_MSG_SAY, wowclient.LANG_COMMON))


class TeleportAckTest(unittest.TestCase):
    def test_far_teleport_is_acked(self):
        c = client(self)
        c.client._observe(wowclient.SMSG_NEW_WORLD, struct.pack("<I4f", 33, 1.0, 2.0, 3.0, 0.0))
        self.assertEqual([opcode for opcode, _ in c.sent()], [wowclient.MSG_MOVE_WORLDPORT_ACK])

    def test_near_teleport_is_acked_with_its_counter(self):
        c = client(self)
        c.client.guid = GRUKK
        # Packed guid (mask 0x01, one byte), the counter, then the movement info.
        c.client._observe(wowclient.MSG_MOVE_TELEPORT_ACK, bytes([0x01, 0x11]) + struct.pack("<I", 7) + b"\0" * 28)
        (opcode, body), = c.sent()
        self.assertEqual(opcode, wowclient.MSG_MOVE_TELEPORT_ACK)
        self.assertEqual(struct.unpack_from("<QI", body), (GRUKK, 7))


class CinematicTest(unittest.TestCase):
    def test_the_intro_of_a_new_character_is_skipped(self):
        c = client(self)
        c.client._observe(wowclient.SMSG_TRIGGER_CINEMATIC, struct.pack("<I", 21))
        self.assertEqual([opcode for opcode, _ in c.sent()], [wowclient.CMSG_COMPLETE_CINEMATIC])


class EchoTest(unittest.TestCase):
    """A check prints the bot chat that follows each command."""

    def test_bot_chat_is_printed_with_the_name(self):
        c = client(self, names={GRUKK: "Grukk"})
        c.client._observe(wowclient.SMSG_MESSAGECHAT, chat_packet(wowclient.CHAT_MSG_PARTY, "We are resting.", sender=GRUKK))
        self.assertEqual(c.echo.getvalue(), "  chat party Grukk: We are resting.\n")

    def test_an_unknown_sender_shows_its_guid(self):
        c = client(self)
        c.client._observe(wowclient.SMSG_MESSAGECHAT, chat_packet(wowclient.CHAT_MSG_WHISPER, "Hi.", sender=0x99))
        self.assertEqual(c.echo.getvalue(), "  chat whisper 0x99: Hi.\n")

    def test_system_lines_are_not_echoed(self):
        c = client(self, names={GRUKK: "Grukk"})
        c.client._observe(wowclient.SMSG_MESSAGECHAT, chat_packet(wowclient.CHAT_MSG_SYSTEM, "X: 1 Y: 2"))
        self.assertEqual(c.echo.getvalue(), "")

    def test_echo_can_be_turned_off(self):
        c = client(self, names={GRUKK: "Grukk"})
        c.client.echo = None
        c.client._observe(wowclient.SMSG_MESSAGECHAT, chat_packet(wowclient.CHAT_MSG_PARTY, "Hi.", sender=GRUKK))
        self.assertEqual(c.echo.getvalue(), "")


ME = 0x0000000000000042
BOAR = 0xF130000C1A000123


def packed_guid(guid):
    mask, data = 0, b""
    for i in range(8):
        byte = (guid >> (8 * i)) & 0xFF
        if byte:
            mask |= 1 << i
            data += bytes([byte])
    return bytes([mask]) + data


class SpellClientTest(unittest.TestCase):
    """The spell packets that the Forever checks read (review T5 of WOWAI#204: one copy, not six)."""

    def spell_client(self):
        c = client(self, client_class=wowclient.SpellClient)
        c.client.guid = ME
        return c

    def test_learned_spells_and_the_login_spellbook(self):
        c = self.spell_client()
        # SMSG_LEARNED_SPELL: uint16 spell, uint16 action bar slot.
        c.client._observe(wowclient.SMSG_LEARNED_SPELL, struct.pack("<HH", 60106, 0))
        # SMSG_INITIAL_SPELLS: uint8 0, uint16 count, count x (uint16 spell, int16 0), uint16 cooldown count.
        c.client._observe(wowclient.SMSG_INITIAL_SPELLS, struct.pack("<BH", 0, 2) + struct.pack("<HhHh", 78, 0, 100, 0) + struct.pack("<H", 0))
        self.assertEqual(c.client.learned, [60106])
        self.assertEqual(c.client.initial_spells, {78, 100})
        self.assertTrue(c.client.knows(60106) and c.client.knows(78))
        self.assertFalse(c.client.knows(60119))

    def test_superseded_spells(self):
        c = self.spell_client()
        c.client._observe(wowclient.SMSG_SUPERCEDED_SPELL, struct.pack("<HH", 100, 60127))
        self.assertEqual(c.client.superseded, [(100, 60127)])

    def test_own_spell_go_and_cast_results(self):
        c = self.spell_client()
        # SMSG_SPELL_GO: packed item or caster guid, packed caster guid, uint32 spell, ...
        c.client._observe(wowclient.SMSG_SPELL_GO, packed_guid(ME) + packed_guid(ME) + struct.pack("<IH", 60123, 0))
        c.client._observe(wowclient.SMSG_SPELL_GO, packed_guid(BOAR) + packed_guid(BOAR) + struct.pack("<IH", 3, 0))
        # SMSG_CAST_RESULT: uint32 spell, uint8 status (2 = failed), uint8 reason.
        c.client._observe(wowclient.SMSG_CAST_RESULT, struct.pack("<IBB", 60123, 2, 18))
        c.client._observe(wowclient.SMSG_CAST_RESULT, struct.pack("<IB", 78, 0))
        self.assertEqual(c.client.spell_go, [60123])
        self.assertEqual(c.client.cast_results, [(60123, 18), (78, None)])

    def test_a_short_packet_is_ignored(self):
        c = self.spell_client()
        c.client._observe(wowclient.SMSG_CAST_RESULT, b"\x01")
        self.assertEqual(c.client.cast_results, [])

    def test_learn_talent_sends_the_talent_and_the_rank(self):
        c = self.spell_client()
        c.client.learn_talent(60002, 2, wait=0)
        self.assertEqual(c.sent(), [(wowclient.CMSG_LEARN_TALENT, struct.pack("<II", 60002, 2))])

    def test_cast_on_self_and_at_a_unit(self):
        c = self.spell_client()
        c.client.cast_at(2687)
        c.client.cast_at(60123, BOAR)
        sent = c.sent()
        self.assertEqual(sent[0], (wowclient.CMSG_CAST_SPELL, struct.pack("<IH", 2687, 0)))
        self.assertEqual(sent[1], (wowclient.CMSG_CAST_SPELL, struct.pack("<IH", 60123, wowclient.TARGET_FLAG_UNIT) + packed_guid(BOAR)))

    def test_set_button(self):
        c = self.spell_client()
        c.client.set_button(115, 60127, wait=0)
        self.assertEqual(c.sent(), [(wowclient.CMSG_SET_ACTION_BUTTON, struct.pack("<BI", 115, 60127))])


if __name__ == "__main__":
    unittest.main()
