#!/usr/bin/env python3
"""In-game check of ticket #90 (the wisp trail of the route walk), on the check kit.

    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t90_wisp_trail.py

The GM Wispwarden walks bots/data/routes/ragefire_chasm.txt (the route of #79,
237 steps) in Ragefire Chasm. Each step answers with a line
"Wisps: N new, M moved, T in the trail (at most 100); <name> has S of L summons."
1. trail: 300 steps within 2 minutes (`.route next` to the last step, then
   `.route back`). Each shown point gets a wisp on each place, the walk
   summons at most 100 wisps, then moves them, and the log has no
   "already has 100 active summons" line.
2. lifetime, near the GM (a wisp counts its time only in the cells near a
   player): a new walk steps between its first two points, 100 wisps, then
   moves them all. When the summons are more than 2 minutes old, but each
   move less, a step finds all 100 wisps. When all wisps timed out, a step
   summons new wisps in their place.
3. new walk: `.route walk` again removes the wisps of the last walk.
The check does not change the route file.
"""
import math
import os
import re
import time

from kit import Realm, Results, pos
from wowclient import CHAT_MSG_SAY, CHAT_MSG_SYSTEM

ACCOUNT = os.environ.get("T90_ACCOUNT", "PLAYER90")
PLAYER = os.environ.get("T90_PLAYER", "Wispwarden")
ROUTE = "ragefire_chasm"
RFC_ENTRANCE = (0.80, -8.23, -15.53)  # areatrigger_teleport 2230
LIFETIME = 120.0
STEPS = 300
WISPS = re.compile(r"Wisps: (\d+) new, (\d+) moved, (\d+) in the trail \(at most (\d+)\); \S+ has (\d+) of (\d+) summons")
PLACES = re.compile(r"(?:Now|Next point): step \d+, point .* (\d+) places\.")
STEP = re.compile(r"Route \S+, step (\d+) of (\d+)\.")

realm = Realm()
results = Results()

c = realm.login(ACCOUNT, PLAYER)
c.set_active_mover()
c.gm(".gm on", select=c.guid)
c.gm(".go xyz %f %f %f 389" % RFC_ENTRANCE, select=c.guid, wait=4)
here = pos(c, c.guid)
results.check("the GM is in Ragefire Chasm", here is not None and math.dist(here, RFC_ENTRANCE) < 5, str(here))


def answers(lines):
    """(places, (new, moved, trail, most, summons, limit)) of each shown point, in order."""
    out, places = [], None
    for line in lines:
        m = PLACES.search(line)
        if m:
            places = int(m.group(1))
        m = WISPS.search(line)
        if m:
            out.append((places, tuple(int(v) for v in m.groups())))
            places = None
    return out


def step(command, wait=1.5):
    lines = c.gm(command, wait=wait)
    got = answers(lines)
    return got[-1][1] if got else None, lines


# A walk to start from, with no wisps of an earlier run.
realm.mark("t90 walk")
first, lines = step(".route walk %s" % ROUTE, wait=2)
total = STEP.search(" ".join(lines))
count = int(total.group(2)) if total else 0
results.check(".route walk loads the route of #79", count > 100 and first is not None, str(lines[:3]))

# 1. 300 steps within 2 minutes. The answers are read at the end.
start_log = len(c.chat_log)
started = time.time()
times = []  # (time, step command) of each step
for k in range(STEPS):
    command = ".route next" if k < count - 1 else ".route back"
    c.chat(CHAT_MSG_SAY, command)
    times.append(time.time())
    c.pump(0.25)
c.pump(3)
took = times[-1] - started
results.check("300 steps in less than 2 minutes", took < LIFETIME, "%.0f s" % took)
lines = [text for (t, _, text) in c.chat_log[start_log:] if t == CHAT_MSG_SYSTEM]
steps = answers(lines)
results.check("each step that shows a point answers with its wisps", len(steps) >= STEPS - 2, "%d answers" % len(steps))
results.check("each place of each shown point gets a wisp (new or moved)",
              all(p is not None and w[0] + w[1] == p for p, w in steps),
              str([s for s in steps if s[0] is None or s[1][0] + s[1][1] != s[0]][:5]))
summoned = (first[0] if first else 0) + sum(w[0] for _, w in steps)
results.check("the walk summons at most 100 wisps", summoned <= 100, "%d summoned" % summoned)
results.check("the trail fills to 100 wisps", max(w[2] for _, w in steps) == 100 and max(w[4] for _, w in steps) <= 100,
              str(steps[-1]))
moved = [i for i, (_, w) in enumerate(steps) if w[1] > 0]
results.check("a full trail moves its oldest wisp: no new wisp after the first move",
              bool(moved) and all(w[0] == 0 for _, w in steps[moved[0]:]), "first move at answer %s" % (moved[:1],))
log = realm.log(all_lines=True)
results.check("the log has no 'already has 100 active summons' line", "active summons" not in log,
              "\n".join(l for l in log.splitlines() if "active summons" in l)[:400])

late = [(i, w) for i, (_, w) in enumerate(steps) if moved and i > moved[0] and w[0] > 0]
print("walk %s to %s; new wisps after the first move: %s" % (time.strftime("%H:%M:%S", time.localtime(started)),
      time.strftime("%H:%M:%S", time.localtime(times[-1])), late[:10]))

# 2. Lifetime and timeout, near the GM: wisp timers run only in the cells
# near a player. A new walk at the entrance; steps 1 and 2 again and again
# (the first points, a few yards from the GM).
c.gm(".route walk %s" % ROUTE, wait=2)


def burst(n):
    """n steps between steps 1 and 2. The wisp answers, read at the end."""
    start = len(c.chat_log)
    for k in range(n):
        c.chat(CHAT_MSG_SAY, ".route next" if k % 2 == 0 else ".route back")
        c.pump(0.3)
    c.pump(2)
    got = [w for _, w in answers([text for (t, _, text) in c.chat_log[start:] if t == CHAT_MSG_SYSTEM])]
    return got + [None] * (n - len(got))


t0 = time.time()
first_burst = burst(60)
summon_end = time.time()
c.pump(max(0.0, t0 + 50 - time.time()))
second_burst = burst(60)
move_end = time.time()
results.check("near the GM, the trail summons 100 wisps, then moves each one",
              all(w is not None for w in first_burst + second_burst)
              and max(w[4] for w in first_burst) == 100 and all(w[0] == 0 for w in second_burst)
              and sum(w[1] for w in second_burst) >= 100,
              "%s ... %s, %d moved" % (first_burst[-1], second_burst[-1], sum(w[1] for w in second_burst if w)))
# All summons are now more than 2 minutes old; each move is less.
c.pump(max(0.0, summon_end + LIFETIME + 5 - time.time()))
w, _ = step(".route next")
results.check("a moved wisp stays 2 minutes from its move: all 100 wisps are there, none new",
              w is not None and w[0] == 0 and w[2] == 100 and w[4] == 100,
              "%s, %.0f s after the last summon, %.0f s after the last move" % (w, time.time() - summon_end, time.time() - move_end))
# All moves are now more than 2 minutes old: each wisp timed out.
c.pump(max(0.0, move_end + LIFETIME + 5 - time.time()))
w, lines = step(".route back")
# The trail keeps only the few wisps that the lifetime step placed.
results.check("after the wisps timed out, the walk summons new ones in their place and the move breaks nothing",
              w is not None and w[0] > 0 and w[1] == 0 and w[2] <= w[0] + 3 and w[4] == w[2], str(w))
w, _ = step(".route next")
results.check("the next step goes on from the new trail", w is not None and w[0] > 0 and w[4] == w[2], str(w))

# 3. A new walk removes the wisps of the last walk.
for _ in range(10):
    step(".route next", wait=0.5)
before, _ = step(".route next")
again, lines = step(".route walk %s" % ROUTE, wait=2)
c.pump(2)
after, _ = step(".route next")
results.check("a new .route walk removes the wisps of the last walk",
              before is not None and before[2] > 3 and again is not None and again[2] == again[0]
              and again[4] == again[2] and after is not None and after[4] == after[2],
              "before %s, new walk %s, next %s" % (before, again, after))

c.gm(".gm off", select=c.guid)
results.finish()
