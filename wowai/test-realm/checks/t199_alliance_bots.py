#!/usr/bin/env python3
"""In-game check of ticket #199 (the Alliance bot set), on the check kit.

    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t199_alliance_bots.py

Run it once on a new lease: on a second run the bots are in the groups of
the players already, so they do not join again.

1. bots: the five Alliance bots exist with their race, class and role, at
   level 16, with the talent build of the class and role, and every item that
   they wear is usable by the Alliance and is a line of their generated
   template that is not for the Horde only.
2. invite: an Alliance player invites the Alliance tank. It joins.
3. other faction: a Horde player asks for the Alliance tank, and the
   Alliance player asks for the Horde tank. Both are refused, and neither
   bot joins.
4. Horde: the Horde player invites the Horde tank. It joins.
"""
import os
import re

from kit import Realm, Results
from wowclient import CHAT_MSG_SYSTEM, CHAT_MSG_WHISPER

ALLIANCE_RACES = (1 << 0) | (1 << 2) | (1 << 3) | (1 << 6)
# name -> (race id, class id, role, gear template and talent build)
BOTS = {
    "Aldric": (1, 1, "tank", "warrior_tank"),
    "Brenna": (3, 5, "healer", "priest_healer"),
    "Corwin": (1, 4, "melee", "rogue_melee"),
    "Fizzwick": (7, 8, "ranged", "mage_ranged"),
    "Nixie": (7, 9, "ranged", "warlock_ranged"),
}
GEAR_DIR = os.path.join(os.path.dirname(__file__), "..", "..", "..", "bots", "data", "gear")

realm = Realm()
results = Results()


def template_entries(name):
    """entry -> faction words of the lines of a gear template ("" = both)."""
    entries = {}
    with open(os.path.join(GEAR_DIR, name + ".txt")) as f:
        for line in f:
            words = line.split("#")[0].split()
            if words[:1] == ["item"]:
                entries.setdefault(int(words[2]), set()).add(words[6] if len(words) > 6 else "")
    return entries


print("== bots")
# The whole Server.log since mangosd started: the bots learned their talents at their first login.
with open(os.path.join(realm.dir, "storage", "mangosd", "logs", "Server.log"), errors="replace") as f:
    log = f.read()


def talent_picks(log, name, build):
    """The talent picks of the last 'learned' line of the bot, or None."""
    found = re.findall(r"%s at level 16 learned (\d+) talent picks of the build %s" % (name, build), log)
    return int(found[-1]) if found else None


for name, (race, cls, role, gear) in BOTS.items():
    row = realm.sql("characters", "SELECT c.race, c.class, c.level, b.role FROM characters c "
                    "JOIN bot_character b ON b.guid = c.guid WHERE c.name = '%s'" % name)
    results.check("%s exists as race %d class %d %s at level 16" % (name, race, cls, role),
                  row == "%d\t%d\t16\t%s" % (race, cls, role), "row %r" % row)
    # A level 16 character has 7 talent points (from level 10), as the Horde bots spend them.
    picks = talent_picks(log, name, gear)
    results.check("%s learns 7 talent picks of the build %s" % (name, gear),
                  picks == 7 and "%s cannot learn" % name not in log, "picks %s" % picks)
    worn = realm.sql("characters", "SELECT ci.item_id FROM character_inventory ci "
                     "JOIN characters c ON c.guid = ci.guid "
                     "WHERE c.name = '%s' AND ci.bag = 0 AND ci.slot < 19" % name).split()
    lines = template_entries(gear)
    results.check("%s wears items of its template" % name, len(worn) >= 5, "items %s" % worn)
    for entry in worn:
        allowed = realm.sql("mangos", "SELECT allowable_race FROM item_template WHERE entry = %s "
                            "ORDER BY patch LIMIT 1" % entry)
        mask = int(allowed)
        results.check("%s: item %s is usable by the Alliance" % (name, entry),
                      mask in (-1, 0) or bool(mask & ALLIANCE_RACES), "allowable_race %d" % mask)
        results.check("%s: item %s is an Alliance or both line of %s" % (name, entry, gear),
                      bool(lines.get(int(entry), set()) & {"", "alliance"}), "lines %s" % lines.get(int(entry)))

print("== invite")
alliance = realm.login("PLAYER199A", "Stormson", race=1, cls=1)
horde = realm.login("PLAYER199H", "Grimson", race=2, cls=1)
realm.mark("invite")
alliance.chat(CHAT_MSG_WHISPER, "invite", target="Aldric")
results.check("the Alliance player invites Aldric, and he joins",
              realm.wait_log(r"Aldric joins the group of Stormson", 20, alliance))


def refused(client, pattern):
    client.pump(3)
    return any(t == CHAT_MSG_SYSTEM and pattern in text for t, _, text in client.chat_log)


print("== other faction")
realm.mark("other faction")
horde.chat(CHAT_MSG_WHISPER, "invite Corwin", target="Grukk")
alliance.chat(CHAT_MSG_WHISPER, "invite Grukk", target="Aldric")
results.check("the Horde player is told that Corwin is of the other faction",
              refused(horde, "That bot is of the other faction."))
results.check("the Alliance player is told that Grukk is of the other faction",
              refused(alliance, "That bot is of the other faction."))
horde.pump(5)
log = realm.log()
results.check("Corwin joins no group", "Corwin joins" not in log and "Corwin logs in" not in log, log[-400:])
results.check("Grukk joins no group", "Grukk joins" not in log, log[-400:])

print("== Horde")
realm.mark("horde")
horde.chat(CHAT_MSG_WHISPER, "invite", target="Grukk")
results.check("the Horde player invites Grukk, and he joins",
              realm.wait_log(r"Grukk joins the group of Grimson", 20, horde))

results.finish()
