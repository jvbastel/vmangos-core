#!/usr/bin/env python3
"""In-game check of ticket #198 (the bot panel of the WowaiRole add-on), on the check kit.

    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t198_bot_panel.py

The headless client sends what the add-on sends: the whisper
"WOWAI_BOTS 1:list" to the player's own name, and the whispers "invite" and
"dismiss" of the Invite and Dismiss buttons. It needs the Horde and the
Alliance bots of the slot (realm_bots in realm.sh).

1. list: a player with no group gets the bots of its faction by role, as
   system messages to the player alone; the server takes the request whisper
   (no whisper line comes back).
2. invite: the panel's Invite on an offline bot logs it in, adds it to the
   group and brings it to the player. Dismiss removes it.
3. two tanks: with the role choice tank, Invite on the bot tank is refused
   with a party line of the bot.
4. typed: `invite <name>` and `dismiss <name>` in party chat still work.
5. plain /invite (CMSG_GROUP_INVITE) of an online bot still works.
"""

from kit import Realm, Results, dist, pos
from wowclient import CHAT_MSG_PARTY, CHAT_MSG_SYSTEM, CHAT_MSG_WHISPER

CMSG_GROUP_INVITE = 110

LANG_ADDON = 0xFFFFFFFF
ALLIANCE = ["Aldric", "Brenna", "Corwin", "Fizzwick", "Nixie"]
HORDE = ["Grukk", "Hela", "Shanka", "Ezzra", "Morzul"]

realm = Realm()
results = Results()


def panel(client):
    """The panel messages that the client got for one list request, and the
    whisper lines that came back (none: the server takes the request)."""
    start = len(client.chat_log)
    client.chat(CHAT_MSG_WHISPER, "WOWAI_BOTS 1:list", target=client_name[client])
    client.pump(3)
    lines = client.chat_log[start:]
    answer = [text.split(" ", 1)[1] for (t, _, text) in lines
              if t == CHAT_MSG_SYSTEM and text.startswith("WOWAI_BOTS ")]
    whispers = [text for (t, _, text) in lines if t == CHAT_MSG_WHISPER]
    return answer, whispers


def in_group(name):
    guid = realm.guid_of(name)
    return realm.sql("characters", "SELECT COUNT(*) FROM group_member WHERE member_guid = %d" % guid) != "0"


def got_party_line(client, start, line):
    return any(t == CHAT_MSG_PARTY and text == line for (t, _, text) in client.chat_log[start:])


alliance = realm.login("PLAYER198A", "Lorason", race=1, cls=1)
horde = realm.login("PLAYER198H", "Grimsson", race=2, cls=1)
client_name = {alliance: "Lorason", horde: "Grimsson"}
# A run before on this lease: no role choice, no bot online, no group.
alliance.chat(CHAT_MSG_PARTY, "WOWAI_ROLE\t1:none", lang=LANG_ADDON)
for name in ALLIANCE + HORDE:
    realm.soap(".wowai bot logout %s" % name)
alliance.pump(5)

print("== list")
realm.mark("list")
results.check("the Alliance player has no group", not in_group("Lorason"))
answer, whispers = panel(alliance)
expected = ["1:clear", "1:bot:tank:Aldric", "1:bot:healer:Brenna", "1:bot:melee:Corwin",
            "1:bot:ranged:Fizzwick", "1:bot:ranged:Nixie", "1:end"]
results.check("with no group, the Alliance player's panel lists the Alliance bots by role", answer == expected,
              "got %r" % answer)
results.check("the request whisper is not sent on", whispers == [], "got %r" % whispers)
answer, _ = panel(horde)
expected = ["1:clear", "1:bot:tank:Grukk", "1:bot:healer:Hela", "1:bot:melee:Shanka",
            "1:bot:ranged:Ezzra", "1:bot:ranged:Morzul", "1:end"]
results.check("with no group, the Horde player's panel lists the Horde bots by role", answer == expected,
              "got %r" % answer)
start = len(alliance.chat_log)
alliance.chat(CHAT_MSG_WHISPER, "WOWAI_BOTS 1:list", target="Grimsson")
alliance.pump(2)
results.check("the request whispered to another player is a plain whisper, no panel",
              not any(t == CHAT_MSG_SYSTEM and text.startswith("WOWAI_BOTS ")
                      for (t, _, text) in alliance.chat_log[start:]))
results.check("the server logs the panel", realm.wait_log(r"Lorason gets the bot panel: 5 bots", 5, alliance))

print("== invite")
# Away from the start place of the Alliance bots (Northshire), so that a bot
# near the player came by the teleport.
alliance.gm(".go xyz -9464 62 56 0", wait=3)
realm.mark("invite")
alliance.chat(CHAT_MSG_WHISPER, "invite", target="Brenna")
results.check("Invite on the offline Brenna logs her in", realm.wait_log(r"Brenna logs in", 20, alliance))
results.check("Brenna joins the group of Lorason", realm.wait_log(r"Brenna joins the group of Lorason", 20, alliance))
alliance.pump(3)
brenna = realm.guid_of("Brenna")
here, there = pos(alliance, alliance.guid), pos(alliance, brenna)
# She follows the player after the teleport: near, not on the spot.
results.check("Brenna comes to Lorason", here and there and dist(here, there) < 20, "%s %s" % (here, there))
realm.mark("dismiss")
alliance.chat(CHAT_MSG_WHISPER, "dismiss", target="Brenna")
results.check("Dismiss removes Brenna", realm.wait_log(r"Brenna leaves the group of Lorason", 20, alliance))

print("== two tanks")
alliance.pump(3)
alliance.chat(CHAT_MSG_PARTY, "WOWAI_ROLE\t1:tank", lang=LANG_ADDON)
alliance.pump(1)
realm.mark("two tanks")
start = len(alliance.chat_log)
alliance.chat(CHAT_MSG_WHISPER, "invite", target="Aldric")
results.check("Invite on Aldric is refused", realm.wait_log(r"Aldric refuses the invite of Lorason: a second tank", 10, alliance))
alliance.pump(2)
results.check("Aldric says the refusal line", got_party_line(alliance, start, "You tank this group, so I stay out of it."))
results.check("Aldric does not join", "Aldric joins" not in realm.log())
alliance.chat(CHAT_MSG_PARTY, "WOWAI_ROLE\t1:none", lang=LANG_ADDON)

print("== typed")
alliance.pump(1)
realm.mark("typed")
alliance.chat(CHAT_MSG_WHISPER, "invite", target="Corwin")
results.check("a typed whisper invite still works", realm.wait_log(r"Corwin joins the group of Lorason", 20, alliance))
alliance.chat(CHAT_MSG_PARTY, "invite Fizzwick")
results.check("a typed `invite <name>` still works", realm.wait_log(r"Fizzwick joins the group of Lorason", 20, alliance))
alliance.chat(CHAT_MSG_PARTY, "dismiss Fizzwick")
results.check("a typed `dismiss <name>` still works", realm.wait_log(r"Fizzwick leaves the group of Lorason", 20, alliance))
alliance.chat(CHAT_MSG_PARTY, "dismiss Corwin")
results.check("the group ends clean", realm.wait_log(r"Corwin leaves the group of Lorason", 20, alliance))

print("== plain /invite")
realm.mark("plain invite")
realm.soap(".wowai bot login Nixie")
results.check("Nixie is online", realm.wait_log(r"Nixie logs in", 20, alliance))
alliance.pump(2)
alliance.send(CMSG_GROUP_INVITE, b"Nixie\0")
alliance.pump(3)
results.check("a plain /invite of Nixie still works", in_group("Nixie") and in_group("Lorason"))
alliance.chat(CHAT_MSG_PARTY, "dismiss Nixie")
results.check("Nixie leaves again", realm.wait_log(r"Nixie leaves the group of Lorason", 20, alliance))

results.finish()
