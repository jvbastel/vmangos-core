#!/usr/bin/env python3
"""In-game check of ticket #42 (death, release, resurrect, and rest), on the check kit.

    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t42_death_rest.py [part]

The player Thrallson invites the bots of `acquire`: the Warrior tank Grukk, the
Priest healer Hela, and the Mage Ezzra. `part`: run one part only (rest,
resurrect, nobody, wipe).

1. Rest: the tank below half health eats, the mage below half mana drinks; the food
   refills silently; the tank does not pull while the group rests.
2. Resurrect: the Priest resurrects the dead mage after the fight; the mage accepts.
3. Nobody can resurrect: with the Priest dead, and with no Priest, a dead bot
   releases to run back as a ghost (#72). It does not revive at its corpse.
4. Wipe: every member dies; the bots wait; the player releases and the bots release;
   the player revives near the corpses, and the ghosts still do not revive there (#72).
"""
import math
import re
import sys

from kit import Realm, Results, pos, stat
from wowclient import CHAT_MSG_PARTY, CHAT_MSG_WHISPER, WorldClient

CMSG_REPOP_REQUEST = 0x15A
SMSG_ITEM_PUSH_RESULT = 0x166
ACCOUNT = "PLAYER42"
PLAYER = "Thrallson"

realm = Realm()
results = Results()
tank, priest, mage = "Grukk", "Hela", "Ezzra"
tank_guid, priest_guid, mage_guid = (realm.guid_of(name) for name in (tank, priest, mage))
bots = {tank: tank_guid, priest: priest_guid, mage: mage_guid}
only = sys.argv[1] if len(sys.argv) > 1 else ""


def rules_of(name, text):
    return re.findall(r"\[Bots\] %s: ([a-z ]+)" % name, text)


class Client(WorldClient):
    """Counts the opcodes that the client receives."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.seen = []

    def _observe(self, opcode, body):
        self.seen.append(opcode)
        super()._observe(opcode, body)


c = realm.login(ACCOUNT, PLAYER, client_class=Client)


def dist(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def go_ground(x, y):
    z = pos(c, c.guid)[2]
    c.gm(".go xyz %f %f %f" % (x, y, z + 1.0), select=c.guid, wait=1.5)
    for line in c.gm(".gps", select=c.guid):
        m = re.search(r"GroundZ: (-?[\d.]+)", line)
        if m:
            c.gm(".go xyz %f %f %f" % (x, y, float(m.group(1)) + 0.1), select=c.guid, wait=1.5)


def revive_all():
    for bot in bots:
        realm.soap(".revive %s" % bot)
    c.gm(".revive", select=c.guid)


def gather():
    for bot in bots:
        c.gm(".namego %s" % bot, wait=2.0)
    c.chat(CHAT_MSG_PARTY, "follow")
    c.pump(6)


def kill(guid):
    return c.gm(".die", select=guid, wait=1.0)


def spawn_mob_here(entry=3098):
    c.gm(".npc spawn add %d" % entry, wait=2.0)
    low = int(realm.sql("mangos", "SELECT MAX(guid) FROM creature WHERE id = %d" % entry).split()[-1])
    return (0xF130 << 48) | (entry << 24) | low


c.gm(".gm off")
# The group: a whisper `invite` logs a bot in and adds it.
for bot in bots:
    c.chat(CHAT_MSG_WHISPER, "invite", target=bot)
    c.pump(4)
members = realm.sql("characters", "SELECT COUNT(*) FROM group_member m JOIN characters c ON c.guid = m.member_guid "
                    "WHERE c.name IN ('%s')" % "', '".join(bots))
results.check("the three bots are in the group of the player", members == "3", "members %s" % members)
revive_all()
# Away from the guards of the Den, who kill a hostile mob.
camp = (-500.0, -4400.0)
go_ground(*camp)
gather()


def part_rest():
    # A mob 40 yards away, for the `pull` while the group rests.
    c.chat(CHAT_MSG_PARTY, "stay")
    c.pump(1)
    go_ground(camp[0] - 40, camp[1])
    mob = spawn_mob_here()
    go_ground(*camp)
    c.chat(CHAT_MSG_PARTY, "follow")
    c.pump(4)

    realm.mark("rest")
    pushes_before = c.seen.count(SMSG_ITEM_PUSH_RESULT)
    health, max_health, _, _ = stat(c, tank_guid)
    print("  tank hp:", c.gm(".modify hp %d %d" % (int(max_health * 0.4), max_health), select=tank_guid))
    _, _, mana, max_mana = stat(c, mage_guid)
    print("  mage mana:", c.gm(".modify mana %d %d" % (int(max_mana * 0.3), max_mana), select=mage_guid))
    ate = realm.wait_log(r"%s: eat" % tank, 6, c)
    drank = realm.wait_log(r"%s: drink" % mage, 6, c)
    text = realm.log()
    results.check("the tank below half health eats after the fight", ate, repr(rules_of(tank, text)))
    results.check("the mage below half mana drinks after the fight", drank, repr(rules_of(mage, text)))
    results.check("the coordinator sees the tank eat", re.search(r"rests: %s eats" % tank, text) is not None)
    results.check("the coordinator sees the mage drink", re.search(r"rests: %s drinks" % mage, text) is not None)
    results.check("the food and the water refill silently",
          ("%s refills item 4542 silently" % tank) in text and ("%s refills item 1205 silently" % mage) in text,
          repr(re.findall(r"refills item \d+", text)))

    # The group waits: the tank does not pull.
    c.select(mob)
    c.pump(0.6)
    chat_start = len(c.chat_log)
    c.chat(CHAT_MSG_PARTY, "pull")
    c.pump(3)
    lines = [text for (t, _, text) in c.chat_log[chat_start:] if t == CHAT_MSG_PARTY]
    results.check("the tank does not pull while the group rests", "We are resting." in lines, repr(lines))
    results.check("the tank stays out of the fight", "%s pulls" % tank not in realm.log())

    h1 = int(max_health * 0.4)
    m1 = int(max_mana * 0.3)
    c.pump(4)
    h2 = stat(c, tank_guid)[0]
    m2 = stat(c, mage_guid)[2]
    results.check("the tank's health rises (its food, or the heals of the Priest)", h2 > h1, "%d -> %d" % (h1, h2))
    results.check("the mage keeps drinking: its mana rises", m2 > m1, "%d -> %d" % (m1, m2))
    text = realm.log()
    results.check("the resting bots do not follow while they rest",
          "keep resting" in " ".join(rules_of(tank, text) + rules_of(mage, text)),
          repr(rules_of(tank, text)))
    results.check("no item push result reaches the player", c.seen.count(SMSG_ITEM_PUSH_RESULT) == pushes_before)
    # The rest ends.
    c.pump(20)
    print("  after the rest: tank", stat(c, tank_guid), "mage", stat(c, mage_guid))
    c.gm(".npc spawn delete", select=mob, wait=2.0)


def part_resurrect():
    realm.mark("resurrect")
    print("  kill mage:", kill(mage_guid))
    ok = realm.wait_log(r"%s: resurrect a member" % priest, 10, c)
    results.check("the Priest resurrects the mage after the fight", ok, repr(rules_of(priest, realm.log())))
    accepted = realm.wait_log(r"%s accepts the resurrect" % mage, 20, c)
    results.check("the mage accepts SMSG_RESURRECT_REQUEST", accepted)
    c.pump(3)
    health = stat(c, mage_guid)[0]
    results.check("the mage is alive again", health > 0, "health %d" % health)
    text = realm.log()
    results.check("the mage did not revive on its own", "%s: revive" % mage not in text, repr(rules_of(mage, text)))


def part_nobody():
    # The Priest dead: nobody can resurrect.
    realm.mark("nobody, priest dead")
    # The mage first: while the Priest lives, the mage waits for its resurrect.
    print("  kill mage:", kill(mage_guid))
    print("  kill priest:", kill(priest_guid))
    ok = realm.wait_log(r"%s: release when no resurrect comes" % mage, 8, c)
    ok_priest = realm.wait_log(r"%s: release when no resurrect comes" % priest, 4, c)
    text = realm.log()
    results.check("with the Priest dead, the mage releases to run back", ok, repr(rules_of(mage, text)))
    results.check("the dead Priest releases to run back too", ok_priest, repr(rules_of(priest, text)))
    results.check("the coordinator sees the release",
          re.search(r"rests: %s releases to run back" % mage, text) is not None)
    c.pump(4)
    text = realm.log()
    results.check("no bot revives at its corpse", all("%s: revive" % bot not in text for bot in bots))
    results.check("the mage is still dead (a ghost)", stat(c, mage_guid)[0] <= 1)
    results.check("the Priest is still dead (a ghost)", stat(c, priest_guid)[0] <= 1)
    revive_all()
    c.pump(2)

    # No Priest in the group (logged out).
    realm.soap(".wowai bot logout %s" % priest)
    c.pump(4)
    realm.mark("nobody, no priest")
    print("  kill mage:", kill(mage_guid))
    # .die deals the damage from the player, so the group stays in combat
    # for some seconds: the mage releases only after that (~9 s).
    ok = realm.wait_log(r"%s: release when no resurrect comes" % mage, 15, c)
    results.check("with no Priest, the mage releases to run back", ok, repr(rules_of(mage, realm.log())))
    c.pump(4)
    results.check("the mage does not revive at its corpse (no Priest)", "%s: revive" % mage not in realm.log())
    results.check("the mage is still dead (no Priest)", stat(c, mage_guid)[0] <= 1)
    revive_all()
    realm.soap(".wowai bot login %s" % priest)
    c.pump(6)
    c.gm(".namego %s" % priest, wait=2.0)
    c.chat(CHAT_MSG_PARTY, "follow")
    c.pump(4)


def part_wipe():
    here = pos(c, c.guid)
    c.chat(CHAT_MSG_PARTY, "stay")
    c.pump(2)
    realm.mark("wipe")
    # The Priest last: while a resurrect can come, the dead wait, so that
    # nobody releases before the wipe (#72).
    print("  kill player:", kill(c.guid))
    for guid in (tank_guid, mage_guid, priest_guid):
        print("  kill:", kill(guid))
    wiped = realm.wait_log(r"Group \d+ wipes", 5, c)
    results.check("the coordinator sees the wipe", wiped)
    c.pump(6)
    text = realm.log()
    results.check("the bots wait while the leader has not released",
          all("%s: release" % bot not in text for bot in bots) and all("%s: revive" % bot not in text for bot in bots),
          repr({bot: rules_of(bot, text) for bot in bots}))

    c.send(CMSG_REPOP_REQUEST)
    released = all(realm.wait_log(r"%s: release with the leader" % bot, 6, c) for bot in bots)
    text = realm.log()
    results.check("when the leader becomes a ghost, the bots release", released, repr({bot: rules_of(bot, text) for bot in bots}))
    c.pump(4)
    ghost_at = pos(c, c.guid)
    print("  player ghost at", ghost_at)
    for bot, guid in bots.items():
        at = pos(c, guid)
        results.check("%s is a ghost away from its corpse" % bot, at is not None and dist(at, here) > 50, "at %r" % (at,))
    text = realm.log()
    results.check("the ghosts do not revive while the leader is a ghost", all("%s: revive" % bot not in text for bot in bots))

    # The player runs back as a ghost and revives near the corpses.
    c.gm(".go xyz %f %f %f" % (here[0], here[1], here[2] + 0.1), select=c.guid, wait=3.0)
    c.pump(3)
    text = realm.log()
    results.check("the ghosts wait while the leader is a ghost near their corpses",
          all("%s: revive" % bot not in text for bot in bots))
    print("  player revive:", c.gm(".revive", select=c.guid, wait=1.0))
    c.pump(8)
    text = realm.log()
    results.check("with the leader alive near the corpses, no bot revives at its corpse",
          all("%s: revive" % bot not in text for bot in bots), repr({bot: rules_of(bot, text) for bot in bots}))
    for bot, guid in bots.items():
        results.check("%s is still a ghost" % bot, stat(c, guid)[0] <= 1)


parts = {"rest": part_rest, "resurrect": part_resurrect, "nobody": part_nobody, "wipe": part_wipe}
for name, part in parts.items():
    if only and name != only:
        continue
    print("== %s" % name)
    part()
    revive_all()
    go_ground(*camp)
    gather()

results.finish()
