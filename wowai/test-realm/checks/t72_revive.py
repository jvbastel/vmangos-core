#!/usr/bin/env python3
"""In-game check of ticket #72 (the revive rule, the ghost run, and the safe Priest resurrect), on the check kit.

    wowai/test-realm/realm.sh --ticket N up
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t72_revive.py [part]

The player Thrallson (a GM) invites the Warrior tank Grukk, the Priest healer
Hela, and the Mage Ezzra. `part`: run one part only (reach, pack, ghost, run).

1. reach: the tank dies out of the reach of the Priest, the mage in reach. The
   Priest resurrects the mage first, then walks to the tank and resurrects it.
2. pack: the mage dies next to a living pack of level 30 tigers. The Priest
   does not go: the mage releases, and no tiger fights.
3. ghost (the play realm): in Ragefire Chasm the Priest and the mage die. Nobody
   can resurrect, so they release, run as ghosts from the Razor Hill graveyard
   to the portal in Orgrimmar, and are alive again at the dungeon entrance.
4. run (a scripted run): a released member revives at once at a living member;
   in a wipe, every member revives at the dungeon entrance.
No bot ever revives at its corpse on its own.
"""
import math
import re
import sys

from kit import Realm, Results, pos, stat
from wowclient import CHAT_MSG_PARTY, CHAT_MSG_WHISPER

ACCOUNT = "PLAYER72"
PLAYER = "Thrallson"
TIGER = 681  # Young Stranglethorn Tiger, level 30: an aggro distance of about 34 yards on a level 16 bot
RFC_ENTRANCE = (0.80, -8.23, -15.53)  # areatrigger_teleport 2230

realm = Realm()
results = Results()
tank, priest, mage = "Grukk", "Hela", "Ezzra"
tank_guid, priest_guid, mage_guid = (realm.guid_of(name) for name in (tank, priest, mage))
bots = {tank: tank_guid, priest: priest_guid, mage: mage_guid}
only = sys.argv[1] if len(sys.argv) > 1 else ""

c = realm.login(ACCOUNT, PLAYER)


def rules_of(name, text):
    return re.findall(r"\[Bots\] %s: ([a-z ]+)" % name, text)


def unit_pos(guid):
    """The position of a unit, or None when the GM cannot select it (then
    `.gps` answers for the GM)."""
    here, there = pos(c, c.guid), pos(c, guid)
    return None if there == here else there


def dist(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def ground_z(x, y, z_hint):
    c.gm(".go xyz %f %f %f" % (x, y, z_hint + 1.0), select=c.guid, wait=1.5)
    for line in c.gm(".gps", select=c.guid):
        m = re.search(r"GroundZ: (-?[\d.]+)", line)
        if m and float(m.group(1)) > -100000:
            return float(m.group(1))
    return z_hint


def go_ground(x, y):
    z = ground_z(x, y, pos(c, c.guid)[2])
    c.gm(".go xyz %f %f %f" % (x, y, z + 0.1), select=c.guid, wait=1.5)


def revive_all():
    for bot in bots:
        realm.soap(".revive %s" % bot)
    c.gm(".revive", select=c.guid)


def gather():
    here = pos(c, c.guid)
    for _ in range(3):
        for bot in bots:
            c.gm(".namego %s" % bot, wait=2.0)
        c.chat(CHAT_MSG_PARTY, "follow")
        c.pump(6)
        # One position read per bot: a second read can find none (a bot in a teleport).
        at = {bot: unit_pos(guid) for bot, guid in bots.items()}
        far = [bot for bot in bots if not at[bot] or dist(at[bot], here) > 15]
        if not far:
            return
        print("  not near yet:", far)


def kill(guid):
    return c.gm(".die", select=guid, wait=1.0)


def die_at(bot, x, y):
    """Summon the bot to (x, y) and kill it there at once."""
    go_ground(x, y)
    c.gm(".namego %s" % bot, wait=1.5)
    print("  kill %s at %r:" % (bot, pos(c, bots[bot])), kill(bots[bot]))


def spawn_here(entry):
    c.gm(".npc spawn add %d" % entry, wait=1.5)
    low = int(realm.sql("mangos", "SELECT MAX(guid) FROM creature WHERE id = %d" % entry).split()[-1])
    return (0xF130 << 48) | (entry << 24) | low


c.gm(".gm on")
for bot in bots:
    c.chat(CHAT_MSG_WHISPER, "invite", target=bot)
    c.pump(4)
members = realm.sql("characters", "SELECT COUNT(*) FROM group_member m JOIN characters c ON c.guid = m.member_guid "
                    "WHERE c.name IN ('%s')" % "', '".join(bots))
results.check("the three bots are in the group of the player", members == "3", "members %s" % members)
revive_all()
camp = (-500.0, -4400.0)
go_ground(*camp)
gather()


def no_self_revive(text):
    return all("%s: revive" % bot not in text and "%s revives at its corpse" % bot not in text for bot in bots)


def part_reach():
    c.chat(CHAT_MSG_PARTY, "stay")
    c.pump(2)
    # The tank comes before the mage in the group: before #72, DeadMember
    # picked only the tank, out of reach, and skipped the mage in reach.
    go_ground(camp[0] + 45, camp[1])
    c.gm(".namego %s" % tank, wait=1.5)
    go_ground(camp[0] + 8, camp[1])
    c.gm(".namego %s" % mage, wait=1.5)
    realm.mark("reach")
    kill(tank_guid)
    kill(mage_guid)
    priest_at = pos(c, priest_guid)
    print("  priest at", priest_at, "tank at", pos(c, tank_guid), "mage at", pos(c, mage_guid))
    second = realm.wait_log(r"%s accepts the resurrect" % tank, 60, c)
    text = realm.log()
    first = text.find("%s accepts the resurrect" % mage)
    results.check("the Priest resurrects the mage in reach first, behind the tank out of reach",
                  0 <= first < text.find("%s accepts the resurrect" % tank) or (first >= 0 and not second),
                  repr(rules_of(priest, text)))
    results.check("then the Priest walks to the tank's corpse out of reach",
                  "%s: go to a corpse out of reach" % priest in text, repr(rules_of(priest, text)))
    results.check("and resurrects the tank", second, repr(rules_of(priest, text)))
    now = pos(c, priest_guid)
    results.check("the Priest moved toward the tank", now is not None and priest_at is not None and dist(now, priest_at) > 5,
                  "from %r to %r" % (priest_at, now))
    results.check("no bot revived on its own", no_self_revive(text))


def part_pack():
    c.chat(CHAT_MSG_PARTY, "stay")
    c.pump(2)
    spot = (camp[0] + 60, camp[1])
    # The pack first, friendly while the mage dies in its middle, so that no
    # tiger fights the mage. Then the tigers are hostile again.
    tigers = []
    for dx, dy in ((3, 0), (-3, 2), (0, -3)):
        go_ground(spot[0] + dx, spot[1] + dy)
        tigers.append(spawn_here(TIGER))
        c.gm(".npc set faction 35", select=tigers[-1], wait=0.5)
    priest_at = unit_pos(priest_guid)
    realm.mark("pack")
    go_ground(*spot)
    c.gm(".namego %s" % mage, wait=1.0)
    kill(mage_guid)
    for tiger in tigers:
        c.gm(".npc set faction 16", select=tiger, wait=0.3)
    go_ground(*camp)
    released = realm.wait_log(r"%s: release when no resurrect comes" % mage, 20, c)
    c.pump(5)
    text = realm.log()
    results.check("next to a living pack, the Priest gives the resurrect up and the mage releases", released,
                  repr({bot: rules_of(bot, text) for bot in (priest, mage)}))
    results.check("the Priest did not resurrect the mage", "%s accepts the resurrect" % mage not in text)
    now = unit_pos(priest_guid)
    results.check("the Priest did not go toward the pack", now and priest_at and dist(now, priest_at) < 10,
                  "from %r to %r" % (priest_at, now))
    full = [t for t in tigers if 0 < stat(c, t)[0] == stat(c, t)[1]]
    results.check("no tiger was pulled: they are at full health", len(full) == len(tigers))
    results.check("the Priest was not attacked", "%s: fade" % priest not in text and "%s: shield itself" % priest not in text,
                  repr(rules_of(priest, text)))
    results.check("the Priest is alive", stat(c, priest_guid)[0] > 0)
    results.check("no bot revived on its own", no_self_revive(text))
    for tiger in tigers:
        c.gm(".npc spawn delete", select=tiger, wait=1.0)


def part_ghost():
    # Into Ragefire Chasm, as a normal group: the play realm rule.
    c.gm(".go xyz %f %f %f 389" % RFC_ENTRANCE, select=c.guid, wait=4.0)
    c.pump(3)
    gather()
    c.chat(CHAT_MSG_PARTY, "stay")
    c.pump(2)
    realm.mark("ghost")
    print("  kill priest:", kill(priest_guid))
    print("  kill mage:", kill(mage_guid))
    released = all(realm.wait_log(r"%s: release when no resurrect comes" % bot, 15, c) for bot in (priest, mage))
    text = realm.log()
    results.check("with the Priest dead, the Priest and the mage release", released,
                  repr({bot: rules_of(bot, text) for bot in (priest, mage)}))
    walking = all(realm.wait_log(r"%s: walk back as a ghost" % bot, 15, c) for bot in (priest, mage))
    results.check("the ghosts walk back", walking)
    # Up to 10 minutes for both, with the place of each ghost every 30 s.
    entered = False
    for _ in range(20):
        text = realm.log()
        entered = all("%s enters map 389 as a ghost" % bot in text for bot in (priest, mage))
        if entered:
            break
        print("  ghosts at", {bot: pos(c, bots[bot]) for bot in (priest, mage)})
        c.pump(30)
    text = realm.log()
    results.check("each ghost runs from the graveyard to the portal and enters", entered,
                  repr(re.findall(r"\[Bots\] \w+ enters map \d+ as a ghost", text)))
    c.pump(6)
    for bot in (priest, mage):
        at = pos(c, bots[bot])
        health = stat(c, bots[bot])[0]
        results.check("%s is alive at the dungeon entrance" % bot,
                      at is not None and dist(at, RFC_ENTRANCE) < 15 and health > 0, "at %r health %d" % (at, health))
    results.check("no bot revived on its own", no_self_revive(text))
    revive_all()
    c.gm(".go xyz %f %f 30 1" % camp, select=c.guid, wait=4.0)
    go_ground(*camp)


def part_run():
    # A scripted run: the driver Ezzra and the bots of `up`.
    realm.mark("run")
    print(" ", realm.soap(".wowai run start ragefire_chasm revive"))
    # A wait line is not logged: the run status names it.
    started = False
    for _ in range(90):
        if "wait 120" in realm.soap(".wowai run status"):
            started = True
            break
        c.pump(1)
    results.check("the run reaches its wait", started, realm.soap(".wowai run status"))
    run_bots = ["Ezzra", "Grukk", "Hela", "Shanka", "Morzul"]
    guids = {name: realm.guid_of(name) for name in run_bots}
    c.gm(".goname Ezzra", wait=4.0)
    c.pump(3)
    # Nobody can resurrect the Priest: it releases, and the run revives it
    # at a living member, not at its corpse and with no ghost run.
    kill(guids["Hela"])
    revived = realm.wait_log(r"Run \d+: Hela revives at \w+", 20, c)
    text = realm.log()
    results.check("in a scripted run, the released Priest revives at once at a living member", revived,
                  repr(rules_of("Hela", text)))
    c.pump(4)
    results.check("the Priest is alive", stat(c, guids["Hela"])[0] > 0)
    results.check("the Priest never walked as a ghost", "Hela enters map" not in text)

    realm.mark("run wipe")
    # The Priest last: while it lives, the dead wait for its resurrect, so
    # nobody releases and revives before every member is dead.
    for name in run_bots:
        if name != "Hela":
            kill(guids[name])
    kill(guids["Hela"])
    wiped = realm.wait_log(r"Run \d+: wipe: everyone revives at the dungeon entrance", 20, c)
    results.check("in a wipe, every member revives at the dungeon entrance", wiped)
    c.gm(".go xyz %f %f %f 389" % RFC_ENTRANCE, select=c.guid, wait=3.0)
    c.pump(4)
    for name in run_bots:
        at = pos(c, guids[name])
        health = stat(c, guids[name])[0]
        results.check("%s is alive at the entrance" % name, at is not None and dist(at, RFC_ENTRANCE) < 15 and health > 0,
                      "at %r health %d" % (at, health))
    print(" ", realm.soap(".wowai run stop"))
    # The group waits for the chat of the run (at most 20 s), then ends:
    # the next part invites the bots again.
    realm.wait_log(r"Run \d+: the members leave the run group", 30, c)
    c.gm(".go xyz %f %f 30 1" % camp, select=c.guid, wait=4.0)


parts = {"reach": part_reach, "pack": part_pack, "ghost": part_ghost, "run": part_run}
for name, part in parts.items():
    if only and name != only:
        continue
    print("== %s" % name)
    part()
    if name != "run":
        revive_all()
        go_ground(*camp)
        gather()

results.finish()
