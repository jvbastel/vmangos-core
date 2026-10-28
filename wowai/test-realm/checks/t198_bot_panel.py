#!/usr/bin/env python3
"""In-game check of ticket #198 (the bot panel of the WowaiRole add-on), on the check kit.

    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t198_bot_panel.py

The headless client sends what the add-on sends: the party add-on message
"WOWAI_BOTS<TAB>1:list", and the whispers "invite" and "dismiss" of the
Invite and Dismiss buttons. It needs the Horde and the Alliance bots of the
slot (realm_bots in realm.sh).

1. list: an Alliance player gets the five Alliance bots by role, a Horde
   player the Horde bots, as party add-on messages to the player alone.
2. invite: the panel's Invite on an offline bot logs it in, adds it to the
   group and brings it to the player. Dismiss removes it.
3. two tanks: with the role choice tank, Invite on the bot tank is refused
   with a party line of the bot.
4. typed: `invite <name>` and `dismiss <name>` in party chat still work.
"""

from kit import Realm, Results, dist, pos
from wowclient import CHAT_MSG_PARTY, CHAT_MSG_WHISPER

LANG_ADDON = 0xFFFFFFFF
ALLIANCE = ["Aldric", "Brenna", "Corwin", "Fizzwick", "Nixie"]
HORDE = ["Grukk", "Hela", "Shanka", "Ezzra", "Morzul"]

realm = Realm()
results = Results()


def panel(client):
    """The panel messages that the client got for one list request."""
    start = len(client.chat_log)
    client.chat(CHAT_MSG_PARTY, "WOWAI_BOTS\t1:list", lang=LANG_ADDON)
    client.pump(3)
    return [text.split("\t", 1)[1] for (t, sender, text) in client.chat_log[start:]
            if t == CHAT_MSG_PARTY and sender == client.guid and text.startswith("WOWAI_BOTS\t")]


def got_party_line(client, start, line):
    return any(t == CHAT_MSG_PARTY and text == line for (t, _, text) in client.chat_log[start:])


alliance = realm.login("PLAYER198A", "Lorason", race=1, cls=1)
horde = realm.login("PLAYER198H", "Grimsson", race=2, cls=1)
# A run before on this lease: no role choice, no bot online, no group.
alliance.chat(CHAT_MSG_PARTY, "WOWAI_ROLE\t1:none", lang=LANG_ADDON)
for name in ALLIANCE + HORDE:
    realm.soap(".wowai bot logout %s" % name)
alliance.pump(5)

print("== list")
realm.mark("list")
answer = panel(alliance)
expected = ["1:clear", "1:bot:tank:Aldric", "1:bot:healer:Brenna", "1:bot:melee:Corwin",
            "1:bot:ranged:Fizzwick", "1:bot:ranged:Nixie", "1:end"]
results.check("the Alliance player's panel lists the Alliance bots by role", answer == expected, "got %r" % answer)
answer = panel(horde)
expected = ["1:clear", "1:bot:tank:Grukk", "1:bot:healer:Hela", "1:bot:melee:Shanka",
            "1:bot:ranged:Ezzra", "1:bot:ranged:Morzul", "1:end"]
results.check("the Horde player's panel lists the Horde bots by role", answer == expected, "got %r" % answer)
results.check("the server logs the panel", realm.wait_log(r"Lorason gets the bot panel: 5 bots", 5, alliance))

print("== invite")
realm.mark("invite")
alliance.chat(CHAT_MSG_WHISPER, "invite", target="Brenna")
results.check("Invite on the offline Brenna logs her in", realm.wait_log(r"Brenna logs in", 20, alliance))
results.check("Brenna joins the group of Lorason", realm.wait_log(r"Brenna joins the group of Lorason", 20, alliance))
alliance.pump(3)
brenna = realm.guid_of("Brenna")
here, there = pos(alliance, alliance.guid), pos(alliance, brenna)
results.check("Brenna comes to Lorason", here and there and dist(here, there) < 10, "%s %s" % (here, there))
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

results.finish()
