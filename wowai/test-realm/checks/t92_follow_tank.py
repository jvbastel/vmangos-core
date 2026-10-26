#!/usr/bin/env python3
"""In-game check of ticket #92 (the group follows the tank to a far pack), on the check kit.

    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t92_follow_tank.py [part]

The player Thrallson (a GM) invites the tank Grukk, the healer Hela, the
rogue Shanka, and the mage Ezzra, says `stay`, and has the tank pull a
Ragefire Trogg. `part`: run one part only (far, short).

1. far: the trogg stands 55 yards away. The tank runs to it. The others
   follow it, but come no nearer to the trogg than its aggro distance before
   the tank fights. In the fight they are near enough to fight and to heal,
   and the trogg dies.
2. short: the trogg stands 16 yards away, in casting range. The tank pulls
   it, and the others do not follow, as before #92.
"""
import math
import re
import sys
import time

from kit import Realm, Results, pos, stat
from wowclient import CHAT_MSG_PARTY, CHAT_MSG_WHISPER

ACCOUNT = "PLAYER92"
PLAYER = "Thrallson"
TROGG = 11318  # Ragefire Trogg, level 13-14
# Less than the aggro distance of a trogg for a level 16 bot: a member this
# near before the tank fights could pull it.
NEAR_TROGG = 10.0

realm = Realm()
results = Results()
tank = "Grukk"
others = ["Hela", "Shanka", "Ezzra"]
bots = {name: realm.guid_of(name) for name in [tank] + others}
only = sys.argv[1] if len(sys.argv) > 1 else ""

c = realm.login(ACCOUNT, PLAYER)


def rules_of(name, text):
    return re.findall(r"\[Bots\] %s: ([a-z ]+)" % name, text)


def dist(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def unit_pos(guid):
    """The position of a unit, or None. The selection and the chat command
    run on different packet queues, so a read can still answer for the GM:
    such a read (the GM's own place) is done again."""
    here = pos(c, c.guid)
    for wait in (0.5, 1.0, 2.0):
        c.select(guid)
        c.pump(wait)
        there = pos(c, None)
        if there and there != here:
            return there
    return None


def quick_pos(guid, gm_at):
    """unit_pos with short waits, for the samples of a fight."""
    for wait in (0.3, 1.0):
        c.select(guid)
        c.pump(wait)
        lines = c.gm(".gps", wait=0.6)
        for line in lines:
            m = re.search(r"X: (-?[\d.]+) Y: (-?[\d.]+) Z: (-?[\d.]+)", line)
            if m:
                there = tuple(float(v) for v in m.groups())
                if there != gm_at:
                    return there
    return None


def select_creature(guid):
    """Select a creature, and wait until the server sees the selection."""
    low = guid & 0xFFFFFF
    for wait in (0.6, 1.0, 2.0, 3.0):
        c.select(guid)
        c.pump(wait)
        if any("Guid: %d)" % low in line for line in c.gm(".npc info")[:1]):
            return True
    return False


def ground_z(x, y, z_hint):
    c.gm(".go xyz %f %f %f 1" % (x, y, z_hint + 1.0), select=c.guid, wait=1.5)
    for line in c.gm(".gps", select=c.guid):
        m = re.search(r"GroundZ: (-?[\d.]+)", line)
        if m and float(m.group(1)) > -100000:
            return float(m.group(1))
    return z_hint


def go_ground(x, y):
    z = ground_z(x, y, pos(c, c.guid)[2])
    c.gm(".go xyz %f %f %f 1" % (x, y, z + 0.1), select=c.guid, wait=1.5)


def spawn_here(entry):
    """Spawn a creature at the GM and return its guid. The row is saved a
    little later, and its guid can be one freed before: find it by place."""
    x, y, _ = pos(c, c.guid)
    c.gm(".npc spawn add %d" % entry, wait=1.5)
    query = ("SELECT guid FROM creature WHERE id = %d AND map = 1 AND POW(position_x - %f, 2) + POW(position_y - %f, 2) < 4 "
             "ORDER BY guid DESC LIMIT 1" % (entry, x, y))
    for _ in range(20):
        low = realm.sql("mangos", query)
        if low:
            return (0xF130 << 48) | (entry << 24) | int(low.split()[-1])
        c.pump(0.5)
    raise RuntimeError("no row for the spawn of %d at %f %f" % (entry, x, y))


def clean_up(entry):
    """Delete the spawns of `entry` that an earlier check left in Durotar."""
    rows = realm.sql("mangos", "SELECT guid, position_x, position_y FROM creature WHERE id = %d AND map = 1" % entry)
    for row in rows.splitlines():
        low, x, y = row.split("\t")
        go_ground(float(x), float(y))
        print("  clean up %s:" % low, c.gm(".npc spawn delete", select=(0xF130 << 48) | (entry << 24) | int(low)))


def gather(camp):
    go_ground(*camp)
    for _ in range(3):
        for bot in bots:
            if stat(c, bots[bot])[0] == 0:
                realm.soap(".revive %s" % bot)
            c.gm(".namego %s" % bot, wait=1.5)
        c.chat(CHAT_MSG_PARTY, "follow")
        c.pump(6)
        at = {bot: unit_pos(guid) for bot, guid in bots.items()}
        far = [bot for bot in bots if not at[bot] or dist(at[bot], camp) > 15]
        if not far:
            return
        print("  not near yet:", far)


def health(guid):
    """The health of a trogg; 0 for one that the GM cannot select any more."""
    return stat(c, None)[0] if select_creature(guid) else 0


def pull(camp, yards, label):
    """Spawn a trogg `yards` east of the camp, say `stay`, and pull it.
    Return the samples {name: pos} over the fight, the troggs, and the log."""
    gather(camp)
    c.chat(CHAT_MSG_PARTY, "stay")
    c.pump(3)
    go_ground(camp[0] + yards, camp[1])
    troggs = [spawn_here(TROGG)]
    go_ground(camp[0] - 5, camp[1])
    start = {bot: unit_pos(guid) for bot, guid in bots.items()}
    print("  start:", start)
    realm.mark(label)
    results.check("the GM selects the trogg (%s)" % label, select_creature(troggs[0]))
    c.chat(CHAT_MSG_PARTY, "pull")
    # The bot reads the selection of the player when it handles the chat,
    # a think later: keep the selection until then.
    results.check("the tank takes the pull (%s)" % label, realm.wait_log(r"\[Bots\] %s (pulls|refuses)" % tank, 15, c)
                  and "%s pulls" % tank in realm.log())
    samples = []
    t0 = time.time()
    gm_at = pos(c, c.guid)
    while time.time() - t0 < 120:
        sample = {bot: quick_pos(guid, gm_at) for bot, guid in bots.items()}
        sample.update({"trogg%d" % i: quick_pos(g, gm_at) for i, g in enumerate(troggs)})
        sample["t"] = time.time() - t0
        samples.append(sample)
        if len(samples) % 2 == 0 and all(health(g) == 0 for g in troggs):
            break
    dead = [g for g in troggs if health(g) == 0]
    text = realm.log()
    for g in troggs:
        c.gm(".npc spawn delete", select=g, wait=1.0)
    return start, samples, troggs, dead, text


def engaged_at(samples):
    """Index of the first sample where the tank stands at a trogg."""
    for i, s in enumerate(samples):
        if s[tank] and any(s.get(k) and dist(s[tank], s[k]) < 6 for k in ("trogg0",)):
            return i
    return None


def part_far(camp):
    start, samples, troggs, dead, text = pull(camp, 55, "far")
    i = engaged_at(samples)
    results.check("the tank reaches the far troggs", i is not None, "%d samples" % len(samples))
    if i is None:
        return
    before = samples[:i]
    nearest = {o: min((dist(s[o], s[k]) for s in before for k in ("trogg0",) if s[o] and s.get(k)),
                      default=999) for o in others}
    print("  nearest to a trogg before the fight:", nearest)
    results.check("before the fight, no member comes within %d yards of a trogg" % NEAR_TROGG,
                  all(d >= NEAR_TROGG for d in nearest.values()), repr(nearest))
    moved = {o: round(max((dist(s[o], start[o]) for s in samples if s[o] and start[o]), default=0), 1)
             for o in others}
    results.check("the members follow the tank", all(d > 15 for d in moved.values()), repr(moved))
    later = [s for s in samples[i:] if s["t"] - samples[i]["t"] >= 4]
    gaps = {o: round(min((dist(s[o], s[tank]) for s in later if s[o] and s[tank]), default=999), 1)
            for o in others}
    results.check("in the fight each member gets within 30 yards of the tank",
                  all(d <= 30 for d in gaps.values()), repr(gaps))
    # The healer may walk to the tank as to a hurt member (#93) instead.
    results.check("the members use the follow rule",
                  all("%s: follow the tank to the pull" % o in text
                      or (o == "Hela" and "%s: go to a hurt member" % o in text) for o in others),
                  repr({o: rules_of(o, text) for o in others}))
    results.check("the trogg dies", len(dead) == 1, "%d dead" % len(dead))
    deaths = [line for line in text.splitlines() if re.search(r"\[Bots\] \w+: (wait dead|release)", line)]
    results.check("no member dies", not deaths, repr(deaths))


def part_short(camp):
    start, samples, troggs, dead, text = pull(camp, 16, "short")
    moved = {o: round(max((dist(s[o], start[o]) for s in samples if s[o] and start[o]), default=0), 1)
             for o in others}
    results.check("on a short pull the others do not follow the tank",
                  all("%s: follow the tank to the pull" % o not in text for o in others),
                  repr({o: rules_of(o, text) for o in others}))
    print("  moved on the short pull:", moved)
    results.check("the trogg dies on the short pull", len(dead) == 1, "%d dead" % len(dead))


c.gm(".gm on")
for bot in bots:
    c.chat(CHAT_MSG_WHISPER, "invite", target=bot)
    c.pump(4)
camp = (-500.0, -4400.0)
go_ground(*camp)
clean_up(TROGG)
if only in ("", "far"):
    part_far(camp)
if only in ("", "short"):
    part_short((camp[0], camp[1] - 40))
results.finish()
