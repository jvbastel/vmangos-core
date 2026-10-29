#!/usr/bin/env python3
"""In-game check of jvbastel/WOWAI#209 (spec #204): no Warrior bot while the Forever rules are on.

Run the vanilla case first: its lease makes the roster bots, Warriors included. The Forever case
re-acquires the same lease, which keeps the characters database, so the Warrior bots exist there.

    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t209_no_warrior_bot.py vanilla
    wowai/test-realm/realm.sh --ticket N acquire --forever
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t209_no_warrior_bot.py forever

vanilla: a new Warrior bot is made, and an existing Warrior bot and an existing bot of another
         class log in. Nothing changes.
forever: `.wowai bot create` of a Warrior is refused with an error line, and no character is made.
         A recipe of another class is still made. `.wowai bot login` of each existing Warrior bot
         is refused with an error line, and the bot stays offline. A bot of another class logs in.
"""
import random
import re
import string
import sys
import time

from kit import Realm, Results

CLASS_WARRIOR = 1
REFUSAL = "No Warrior bot: the Forever rules are on, and the player plays the Warrior."

mode = sys.argv[1] if len(sys.argv) > 1 else ""
if mode not in ("forever", "vanilla"):
    sys.exit("usage: t209_no_warrior_bot.py forever|vanilla")

realm = Realm()
results = Results()


def new_name(prefix):
    """A character name that is not taken yet: a run can repeat on the same lease."""
    return prefix + "".join(random.choice(string.ascii_lowercase) for _ in range(5))


def online(name):
    return realm.sql("characters", "SELECT online FROM characters WHERE name = '%s'" % name) == "1"


def logs_in(name):
    """Log a roster bot in. The SOAP answer, and whether it came into the world."""
    if online(name):
        realm.soap(".wowai bot logout %s" % name)
        time.sleep(3)
    realm.mark("login %s" % name)
    answer = realm.soap(".wowai bot login %s" % name)
    return answer, realm.wait_log(r"\[Bots\] %s logs in" % name, 30)


roster = realm.roster()
warriors = [bot.name for bot in roster if bot.cls == CLASS_WARRIOR]
others = [bot.name for bot in roster if bot.cls != CLASS_WARRIOR]
results.check("the roster has a Warrior bot and a bot of another class", bool(warriors and others),
              "warriors %r, others %r (run the vanilla case first, on the same lease)" % (warriors, others))
if not (warriors and others):
    results.finish()

other = others[0]
made = []

if mode == "forever":
    realm.mark("create warrior")
    name = new_name("Nowar")
    answer = realm.soap(".wowai bot create %s warrior tank" % name)
    results.check("a Warrior recipe is refused with an error line", REFUSAL in answer, "answer %r" % answer)
    results.check("the refused Warrior has no character", realm.guid_of(name) == 0)
    results.check("the server log names the refusal",
                  realm.wait_log(r"\[Bots\] Refused the bot %s: No Warrior bot" % name, 5))

    name = new_name("Okpri")
    answer = realm.soap(".wowai bot create %s priest healer" % name)
    made.append(name)
    results.check("a recipe of another class is still made", "made" in answer and realm.guid_of(name) != 0,
                  "answer %r" % answer)

    for warrior in warriors:
        realm.mark("login %s" % warrior)
        answer = realm.soap(".wowai bot login %s" % warrior)
        results.check("the existing Warrior bot %s is refused with an error line" % warrior, REFUSAL in answer,
                      "answer %r" % answer)
        results.check("the server log names the refused login of %s" % warrior,
                      realm.wait_log(r"\[Bots\] %s does not log in: No Warrior bot" % warrior, 5))
        time.sleep(3)
        results.check("%s stays offline" % warrior,
                      not online(warrior) and not re.search(r"\[Bots\] %s logs in" % warrior, realm.log()))

    answer, ok = logs_in(other)
    results.check("the bot %s of another class logs in" % other, "logs in" in answer and ok, "answer %r" % answer)
else:
    name = new_name("Vanwar")
    answer = realm.soap(".wowai bot create %s warrior melee" % name)
    made.append(name)
    results.check("a Warrior recipe is made", "made" in answer and realm.guid_of(name) != 0, "answer %r" % answer)

    answer, ok = logs_in(warriors[0])
    results.check("the existing Warrior bot %s logs in" % warriors[0], "logs in" in answer and ok,
                  "answer %r" % answer)
    answer, ok = logs_in(other)
    results.check("the bot %s of another class logs in" % other, "logs in" in answer and ok, "answer %r" % answer)

# Leave the bots offline, as the lease started.
for name in made + warriors[:1] + [other]:
    if online(name):
        realm.soap(".wowai bot logout %s" % name)

results.finish()
