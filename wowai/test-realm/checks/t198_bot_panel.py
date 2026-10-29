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
3. two tanks: with the role choice tank (the add-on's whisper to the
   player's own name), Invite on the bot tank, with no group, is refused
   with a whisper of the bot (spec #188 fix-up S4).
4. typed: `invite <name>` and `dismiss <name>` in party chat still work.
5. plain /invite (CMSG_GROUP_INVITE) of an online bot still works.
"""

from kit import Realm, Results, dist, pos
from wowclient import CHAT_MSG_PARTY, CHAT_MSG_SYSTEM, CHAT_MSG_WHISPER

CMSG_GROUP_INVITE = 110

realm = Realm()
results = Results()
ALLIANCE = realm.roster("alliance")
HORDE = realm.roster("horde")
TANK = realm.bot("tank", "alliance")
HEALER = realm.bot("healer", "alliance")
MELEE = realm.bot("melee", "alliance")
RANGED = realm.bot("ranged", "alliance")
RANGED2 = realm.bot("ranged", "alliance", 1)


def panel_list(bots):
    return ["1:clear"] + ["1:bot:%s:%s" % (bot.role, bot.name) for bot in bots] + ["1:end"]


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


def got_line(client, start, kind, line):
    return any(t == kind and text == line for (t, _, text) in client.chat_log[start:])


def role_choice(client, role):
    """What the add-on sends: a whisper to the player's own name."""
    client.chat(CHAT_MSG_WHISPER, "WOWAI_ROLE 1:%s" % role, target=client_name[client])


alliance = realm.login("PLAYER198A", "Lorason", race=1, cls=1)
horde = realm.login("PLAYER198H", "Grimsson", race=2, cls=1)
client_name = {alliance: "Lorason", horde: "Grimsson"}
# A run before on this lease: no role choice, no bot online, no group.
role_choice(alliance, "none")
for bot in ALLIANCE + HORDE:
    realm.soap(".wowai bot logout %s" % bot.name)
alliance.pump(5)

print("== list")
realm.mark("list")
results.check("the Alliance player has no group", not in_group("Lorason"))
answer, whispers = panel(alliance)
expected = panel_list(ALLIANCE)
results.check("with no group, the Alliance player's panel lists the Alliance bots by role", answer == expected,
              "got %r" % answer)
results.check("the request whisper is not sent on", whispers == [], "got %r" % whispers)
answer, _ = panel(horde)
expected = panel_list(HORDE)
results.check("with no group, the Horde player's panel lists the Horde bots by role", answer == expected,
              "got %r" % answer)
start = len(alliance.chat_log)
alliance.chat(CHAT_MSG_WHISPER, "WOWAI_BOTS 1:list", target="Grimsson")
alliance.pump(2)
results.check("the request whispered to another player is a plain whisper, no panel",
              not any(t == CHAT_MSG_SYSTEM and text.startswith("WOWAI_BOTS ")
                      for (t, _, text) in alliance.chat_log[start:]))
results.check("the server logs the panel",
              realm.wait_log(r"Lorason gets the bot panel: %d bots" % len(ALLIANCE), 5, alliance))

print("== invite")
# Away from the start place of the Alliance bots (Northshire), so that a bot
# near the player came by the teleport.
alliance.gm(".go xyz -9464 62 56 0", wait=3)
realm.mark("invite")
alliance.chat(CHAT_MSG_WHISPER, "invite", target=HEALER)
results.check("Invite on the offline healer logs it in", realm.wait_log(r"%s logs in" % HEALER, 20, alliance))
results.check("the healer joins the group of Lorason", realm.wait_log(r"%s joins the group of Lorason" % HEALER, 20, alliance))
alliance.pump(3)
healer = realm.guid_of(HEALER)
here, there = pos(alliance, alliance.guid), pos(alliance, healer)
# It follows the player after the teleport: near, not on the spot.
results.check("the healer comes to Lorason", here and there and dist(here, there) < 20, "%s %s" % (here, there))
realm.mark("dismiss")
alliance.chat(CHAT_MSG_WHISPER, "dismiss", target=HEALER)
results.check("Dismiss removes the healer", realm.wait_log(r"%s leaves the group of Lorason" % HEALER, 20, alliance))

print("== two tanks")
alliance.pump(3)
start = len(alliance.chat_log)
role_choice(alliance, "tank")
alliance.pump(1)
results.check("the server takes the role choice", realm.wait_log(r"Lorason sends the role choice \"1:tank\"", 5, alliance))
results.check("the role choice whisper is not sent on", not got_line(alliance, start, CHAT_MSG_WHISPER, "WOWAI_ROLE 1:tank"))
realm.mark("two tanks")
results.check("Lorason has no group", not in_group("Lorason"))
start = len(alliance.chat_log)
alliance.chat(CHAT_MSG_WHISPER, "invite", target=TANK)
results.check("Invite on the bot tank is refused",
              realm.wait_log(r"%s refuses the invite of Lorason: a second tank" % TANK, 10, alliance))
alliance.pump(2)
results.check("with no group, the bot tank whispers the refusal line",
              got_line(alliance, start, CHAT_MSG_WHISPER, "You tank this group, so I stay out of it."))
results.check("the bot tank does not join", "%s joins" % TANK not in realm.log())
role_choice(alliance, "none")

print("== typed")
alliance.pump(1)
realm.mark("typed")
alliance.chat(CHAT_MSG_WHISPER, "invite", target=MELEE)
results.check("a typed whisper invite still works", realm.wait_log(r"%s joins the group of Lorason" % MELEE, 20, alliance))
alliance.chat(CHAT_MSG_PARTY, "invite " + RANGED)
results.check("a typed `invite <name>` still works", realm.wait_log(r"%s joins the group of Lorason" % RANGED, 20, alliance))
alliance.chat(CHAT_MSG_PARTY, "dismiss " + RANGED)
results.check("a typed `dismiss <name>` still works", realm.wait_log(r"%s leaves the group of Lorason" % RANGED, 20, alliance))
alliance.chat(CHAT_MSG_PARTY, "dismiss " + MELEE)
results.check("the group ends clean", realm.wait_log(r"%s leaves the group of Lorason" % MELEE, 20, alliance))

print("== plain /invite")
realm.mark("plain invite")
realm.soap(".wowai bot login " + RANGED2)
results.check("the bot is online", realm.wait_log(r"%s logs in" % RANGED2, 20, alliance))
alliance.pump(2)
alliance.send(CMSG_GROUP_INVITE, RANGED2.encode() + b"\0")
alliance.pump(3)
results.check("a plain /invite of an online bot still works", in_group(RANGED2) and in_group("Lorason"))
alliance.chat(CHAT_MSG_PARTY, "dismiss " + RANGED2)
results.check("the bot leaves again", realm.wait_log(r"%s leaves the group of Lorason" % RANGED2, 20, alliance))

results.finish()
