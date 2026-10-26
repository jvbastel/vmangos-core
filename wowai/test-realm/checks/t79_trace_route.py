#!/usr/bin/env python3
"""In-game check of ticket #79 (the GM trace and the route build), on the check kit.

    wowai/test-realm/realm.sh --ticket N up
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t79_trace_route.py

1. trace: the GM Thrallson walks in Ragefire Chasm (client movement packets
   along the first points of bots/data/routes/ragefire_chasm.txt) with
   `.route trace start`. Each command answers with its [route] line. A
   teleport and a flight stay out of the trace with a warning; a mark, a
   pause and a far resume, and an undo work. The trace file reads back.
2. build: `.route build` over SOAP (no character) writes the route file from
   the trace, prints the summary, and reports the bosses and the quest
   objectives that the short trace misses, with the nearest packs. Each
   route point and pull point lies on the trace, and each checkpoint is a
   route point at its exact position.
3. whole: a stand-in trace along all points of the generated route of #58
   (not a GM walk) builds a route with the four bosses and no unmet boss.
4. walk: `.route walk` and `.route next` load the built file.
The check deletes its trace and route files at the end.
"""
import math
import os
import re
import time

from kit import Realm, Results, pos
from wowclient import MSG_MOVE_HEARTBEAT, MOVEFLAG_FORWARD

ACCOUNT = "PLAYER79"
PLAYER = "Thrallson"
ROUTE = "t79_trace"
WHOLE = "t79_whole"
RFC_ENTRANCE = (0.80, -8.23, -15.53)  # areatrigger_teleport 2230
MOVEFLAG_FLYING = 0x01000000
STATUS = re.compile(r"\[route\] trace (on|paused|off) (\S+) points=(\d+) marks=(\d+) length=(\d+)y")

realm = Realm()
results = Results()
here = os.path.dirname(os.path.abspath(__file__))
routes_dir = os.path.join(realm.settings.get("WOWAI_BOTS_DATA") or os.path.join(here, "..", "..", "..", "bots", "data"), "routes")
if not os.path.isdir(routes_dir):
    routes_dir = os.path.join(here, "..", "..", "..", "bots", "data", "routes")


def route_points(path):
    """The first places of the points of a route file, in order: (name, (x, y, z))."""
    points = []
    for line in open(path):
        words = line.split("#")[0].split()
        if words and words[0] == "point":
            points.append((words[1], tuple(float(v) for v in words[2:5])))
    return points


def trace_points(path):
    """(kind, (x, y, z), joined) of each trace point."""
    out, gap = [], True
    for line in open(path):
        words = line.split("#")[0].split()
        if not words:
            continue
        if words[0] in ("gap", "walk"):
            gap = True
        elif words[0] in ("p", "mark"):
            out.append((words[0], tuple(float(v) for v in words[1:4]), not gap))
            gap = False
    return out


def seg_dist(p, a, b):
    v = [b[i] - a[i] for i in range(3)]
    l2 = sum(c * c for c in v)
    t = 0.0 if l2 == 0 else max(0.0, min(1.0, sum((p[i] - a[i]) * v[i] for i in range(3)) / l2))
    return math.dist(p, [a[i] + v[i] * t for i in range(3)])


def off_trace(p, trace):
    best = min(math.dist(p, q) for _, q, _ in trace)
    for i in range(1, len(trace)):
        if trace[i][2]:
            best = min(best, seg_dist(p, trace[i - 1][1], trace[i][1]))
    return best


def status_of(lines):
    for line in lines:
        m = STATUS.search(line)
        if m:
            return m.group(1), int(m.group(3)), int(m.group(4)), int(m.group(5))
    return None


c = realm.login(ACCOUNT, PLAYER)
c.set_active_mover()
c.gm(".gm on", select=c.guid)
c.gm(".go xyz %f %f %f 389" % RFC_ENTRANCE, select=c.guid, wait=4)
here_pos = pos(c, c.guid)
results.check("the GM is in Ragefire Chasm", here_pos is not None and math.dist(here_pos, RFC_ENTRANCE) < 5, str(here_pos))

for name in (ROUTE, WHOLE):
    for suffix in (".txt", ".trace.txt"):
        path = os.path.join(routes_dir, name + suffix)
        if os.path.exists(path):
            os.remove(path)

at = list(RFC_ENTRANCE)


def walk_to(target, flags=MOVEFLAG_FORWARD, speed=6.0):
    """Client movement packets in steps of about 1 yard."""
    global at
    steps = max(1, int(math.dist(at, target)))
    for k in range(1, steps + 1):
        p = [at[i] + (target[i] - at[i]) * k / steps for i in range(3)]
        c.move(MSG_MOVE_HEARTBEAT, p[0], p[1], p[2], 0.0, flags)
        c.pump(1.0 / speed)
    at = list(target)


# 1. trace
realm.mark("t79 trace")
lines = c.gm(".route trace start %s" % ROUTE)
results.check("start answers the on line", status_of(lines) is not None and status_of(lines)[0] == "on", str(lines))
generated = route_points(os.path.join(routes_dir, "ragefire_chasm.txt"))
path = [p for _, p in generated[:12]]
for target in path[1:6]:
    walk_to(target)
s = status_of(c.gm(".route trace show"))
results.check("the walk adds points about each 4 yards", s is not None and s[1] >= 5 and s[3] >= 15, str(s))
walked = s[3] if s else 0
lines = c.gm(".route trace mark the first corner")
s = status_of(lines)
results.check("mark adds a checkpoint", s is not None and s[2] == 1, str(lines))
mark_at = tuple(at)

# A teleport: the trace leaves it out and warns.
far = path[9]
c.gm(".go xyz %f %f %f 389" % far, select=c.guid, wait=3)
at = list(far)
c.pump(1)
results.check("a teleport gets a warning", realm.wait_log(r"trace warning: .*(jump|off the map)", 10, c), realm.log()[-600:])
walk_to(path[10])
s = status_of(c.gm(".route trace show"))
results.check("the teleport adds no length", s is not None and s[3] < walked + 25, "%s, walked %d" % (s, walked))

# A flight: no points, a warning once.
realm.mark("t79 fly")
c.gm(".cheat fly on", select=c.guid)
before = status_of(c.gm(".route trace show"))
fly_from = list(at)
for k in range(1, 9):
    c.move(MSG_MOVE_HEARTBEAT, fly_from[0] + k, fly_from[1], fly_from[2] + 3, 0.0, MOVEFLAG_FORWARD | MOVEFLAG_FLYING)
    c.pump(0.3)
after = status_of(c.gm(".route trace show"))
results.check("a flight gets a warning", realm.wait_log(r"trace warning: you fly", 5, c), realm.log()[-400:])
results.check("a flight adds no points", before and after and after[1] == before[1], "%s -> %s" % (before, after))
c.gm(".cheat fly off", select=c.guid)
c.move(MSG_MOVE_HEARTBEAT, fly_from[0], fly_from[1], fly_from[2], 0.0, 0)
c.pump(0.5)
at = fly_from

# Pause, a walk, and a far resume.
lines = c.gm(".route trace pause")
results.check("pause answers paused", status_of(lines) is not None and status_of(lines)[0] == "paused", str(lines))
paused = status_of(lines)
walk_to(path[11])
walk_to(path[10])
walk_to(path[11])
s = status_of(c.gm(".route trace show"))
results.check("a paused trace adds no points", s is not None and paused and s[1] == paused[1], "%s %s" % (paused, s))
lines = c.gm(".route trace resume")
results.check("resume answers on", status_of(lines) is not None and status_of(lines)[0] == "on", str(lines))
results.check("a far resume gets a gap warning", any("trace warning: a gap" in l for l in lines), str(lines))
walk_to(path[10])

# Undo.
before = status_of(c.gm(".route trace show"))
lines = c.gm(".route trace undo 6")
s = status_of(lines)
results.check("undo removes the end", s is not None and before and s[1] < before[1], "%s -> %s" % (before, s))
lines = c.gm(".route trace stop")
results.check("stop answers off", status_of(lines) is not None and status_of(lines)[0] == "off", str(lines))
lines = c.gm(".route trace show")
results.check("show after stop is an error line", any(l.startswith("[route] trace error: no trace is on") for l in lines), str(lines))

trace_file = os.path.join(routes_dir, ROUTE + ".trace.txt")
trace = trace_points(trace_file) if os.path.exists(trace_file) else []
results.check("the trace file is written", len(trace) >= 8, "%d points" % len(trace))
marks = [p for kind, p, _ in trace if kind == "mark"]
results.check("the trace file has the checkpoint", len(marks) == 1 and math.dist(marks[0], mark_at) < 3, str(marks))
results.check("the trace has a gap at the teleport and at the resume", sum(1 for i, t in enumerate(trace) if i and not t[2]) >= 2,
              str([i for i, t in enumerate(trace) if i and not t[2]]))
longest = max((math.dist(trace[i - 1][1], trace[i][1]) for i in range(1, len(trace)) if trace[i][2]), default=0)
results.check("no joined step of the trace is a jump", longest < 10, "%.1f" % longest)

# 2. build over SOAP, with no character.
answer = realm.soap(".route build %s bosses=11517,11519" % ROUTE)
print(answer)
results.check("build writes the route file", "Route %s on map 389 from the trace" % ROUTE in answer, answer[:300])
results.check("build prints the packs and quests", "Packs in order:" in answer and "Quests:" in answer)
results.check("build reports the bosses that the trace misses", "Unmet: the trace does not pass the boss 11519" in answer)
results.check("build reports an unmet kill count with the nearest packs",
              re.search(r"Unmet: quest 5723 .*the trace has \d+ of 8", answer) is not None and "Nearest:" in answer)
route_file = os.path.join(routes_dir, ROUTE + ".txt")
built = route_points(route_file) if os.path.exists(route_file) else []
results.check("the built route has points", len(built) >= 3, str(len(built)))
worst = max((off_trace(p, trace) for _, p in built), default=99)
results.check("each route point and pull point lies on the trace", worst < 0.05, "%.3f yards" % worst)
results.check("the checkpoint is a route point at its exact position",
              any(n.startswith("mark") and marks and math.dist(p, marks[0]) < 0.01 for n, p in built), str(built[:5]))
pulls = [n for n, _ in built if n.startswith("pull_")]
results.check("the packs on the trace have pull points", len(pulls) >= 1, str(pulls))

# 3. A stand-in trace along the whole generated route.
with open(os.path.join(routes_dir, WHOLE + ".trace.txt"), "w") as f:
    f.write("trace %s 389\n" % WHOLE)
    prev = None
    for _, p in generated:
        if prev is not None:
            n = max(1, int(math.dist(prev, p) / 4))
            for k in range(1, n):
                q = [prev[i] + (p[i] - prev[i]) * k / n for i in range(3)]
                f.write("p %.2f %.2f %.2f\n" % tuple(q))
        f.write("p %.2f %.2f %.2f\n" % p)
        prev = p
answer = realm.soap(".route build %s bosses=11517,11520,11518,11519" % WHOLE)
print(answer)
results.check("the whole trace builds", "Route %s on map 389 from the trace" % WHOLE in answer, answer[:300])
results.check("the whole trace passes the four bosses", "does not pass the boss" not in answer)
results.check("the whole trace meets each quest objective", "Each boss and each quest objective is on the trace." in answer)
for boss in ("oggleflint", "taragaman_the_hungerer", "jergosh_the_invoker", "bazzalan"):
    results.check("the whole route has the pack %s" % boss, re.search(r"\s%s: \d" % boss, answer) is not None)
for quest in (5722, 5723, 5724, 5725, 5728, 5761):
    results.check("the whole route lists the quest %d" % quest, re.search(r"\s%d [A-Z]" % quest, answer) is not None)

# 4. walk
lines = c.gm(".route walk %s" % ROUTE, wait=2)
results.check(".route walk loads the built file", any(ROUTE in l for l in lines), str(lines))
lines = c.gm(".route next", wait=2)
results.check(".route next goes on", len(lines) > 0, str(lines))

c.gm(".gm off", select=c.guid)
for name in (ROUTE, WHOLE):
    for suffix in (".txt", ".trace.txt"):
        p = os.path.join(routes_dir, name + suffix)
        if os.path.exists(p) and not os.environ.get("T79_KEEP"):
            os.remove(p)
results.finish()
