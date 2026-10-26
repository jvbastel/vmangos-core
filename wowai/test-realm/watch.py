#!/usr/bin/env python3
"""Follow the scripted runs of a realm on a phone (jvbastel/WOWAI#98).

    wowai/test-realm/watch.py --ticket <N> [--port 8098] [--count <M>] [--ntfy <topic>]
    wowai/test-realm/watch.py --shared ...

Options:
    --shared            The shared test realm instead of the realm of a ticket.
    --port <port>       The port of the page, on every address. Default: 8098.
    --count <M>         The runs that runs.py starts, for "run N of M".
    --ntfy <topic>      Also push the run start, each wipe, each boss kill,
                        each member that did not join a fight, and the run
                        end to https://ntfy.sh/<topic>, at most one push of
                        each kind per 30 s. Anyone who knows the topic can
                        read it: choose a random name.
    --runs-dir <folder> The host folder of Bots.Run.ResultDir, when the realm
                        sets one. Default: <realm folder>/storage/mangosd/logs/runs.
    --status            Ask `.wowai run status` over SOAP every 10 s for the
                        phase of the run.

The script reads two files that mangosd writes, and needs no SOAP: the
`[Bots] Run` lines of <realm folder>/storage/mangosd/logs/Server.log (the
script steps, deaths, wipes, boss kills, threat losses, chases, and the fight
summary of each fight), and the live trace of the run, live.trace.txt in the
runs folder (the target and the casts of each member). It serves one page,
which reloads every 5 s, and prints the address to open on the phone.
`runs.py --watch` starts it next to the runs. See
wowai/docs/watch-a-run.md for the firewall rule of Windows.
"""
import argparse
import html
import http.server
import json
import os
import re
import socket
import sys
import threading
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "check"))

DEFAULT_PORT = 8098
REFRESH_SECONDS = 5
READ_SECONDS = 1
STATUS_SECONDS = 10
LIVE_TRACE = "live.trace.txt"
EVENTS_SHOWN = 30
FIGHTS_SHOWN = 6
PUSH_EVERY_SECONDS = 30
PUSHED_KINDS = ("run_start", "wipe", "boss_kill", "did_not_join", "run_end")

LOG = re.compile(r"^\d{4}-\d\d-\d\d (\d\d:\d\d:\d\d) \[Bots\] Run (\d+)(:| \S+ \S+:) (.*)$")
STARTS = re.compile(r"^(\S+) (\S+) starts, test driver (\S+)")
END = re.compile(r"^(passed|failed|aborted) after ")
SCRIPT_LINE = re.compile(r"^line \d+: (.*)")
FIGHT_SUMMARY = re.compile(r"^fight (\d+) (\S+)( again)?: (.*)$")
RESULT = re.compile(r"^result \S*?([^/\s]+\.json)")
DIES = re.compile(r"^(\S+) dies$")
REVIVES = re.compile(r"^(\S+) revives at ")
RESET = re.compile(r"^reset (\S+): level ")
TRACE = re.compile(r"^(\d+\.\d) (.*)$")
TRACE_MEMBER = re.compile(r"^(\S+) (at|targets|casts) (.*)$")


class Member:
    def __init__(self):
        self.alive = True
        self.target = ""
        self.spell = ""


class Fight:
    def __init__(self, number, pack, joined):
        self.number = number
        self.pack = pack
        self.joined = joined  # Member name -> took part.


class RunState:
    """What the page shows: the newest run, from its log and trace lines.
    `on_event(kind, text)` hears each new event, for the pushes."""

    def __init__(self, on_event=None):
        self.on_event = on_event
        self.count = None
        self.runs_seen = 0
        self.status = ""
        self._new_run(0, "", "")

    def _new_run(self, number, script, scenario):
        self.number = number
        self.script = script
        self.scenario = scenario
        self.line = ""
        self.pack = ""
        self.seconds = 0.0
        self.members = {}
        self.fights = []
        self.events = []
        self.outcome = None
        self.end = ""
        self.result_file = None
        self.threshold_failures = None  # Read from the result file at the end.

    def _member(self, name):
        return self.members.setdefault(name, Member())

    def _event(self, clock, kind, text):
        self.events.append((clock, kind, text))
        if self.on_event:
            self.on_event(kind, text)

    def feed_log(self, line):
        match = LOG.match(line.rstrip("\r\n"))
        if not match:
            return
        clock, number, head, text = match.groups()
        number = int(number)
        if head != ":":
            # "Run N <script> <scenario>: passed after ...": the end line.
            ended = END.match(text)
            if ended and number == self.number:
                self.outcome = ended.group(1)
                self.end = "Run %d%s %s" % (number, head, text)
                self._event(clock, "run_end", self.end)
            return
        started = STARTS.match(text)
        if started:
            self.runs_seen += 1
            self._new_run(number, started.group(1), started.group(2))
            self._member(started.group(3))
            self._event(clock, "run_start", "Run %d %s %s starts" % (number, self.script, self.scenario))
            return
        if number != self.number:
            return
        step = SCRIPT_LINE.match(text)
        summary = FIGHT_SUMMARY.match(text)
        if step:
            self.line = text
            command = step.group(1)
            if command.startswith("fight "):
                self.pack = command.split()[1]
                self._event(clock, "pull", command)
        elif summary:
            self._fight(clock, summary)
        elif text.startswith("wipe: "):
            for member in self.members.values():
                member.alive = True
        elif text.startswith("wipe "):
            self._event(clock, "wipe", text)
        elif text.startswith("boss_kill "):
            self._event(clock, "boss_kill", text)
        elif text.startswith("threat loss: "):
            self._event(clock, "threat_loss", text)
        elif " chases " in text:
            self._event(clock, "chase", text)
        elif text.startswith("Fire Nova hits "):
            self._event(clock, "fire_nova", text)
        elif DIES.match(text):
            self._member(DIES.match(text).group(1)).alive = False
            self._event(clock, "death", text)
        elif REVIVES.match(text):
            self._member(REVIVES.match(text).group(1)).alive = True
        elif RESET.match(text):
            self._member(RESET.match(text).group(1))
        elif RESULT.match(text):
            self.result_file = RESULT.match(text).group(1)

    def _fight(self, clock, summary):
        number, pack, again, parts = summary.groups()
        name = "%s%s" % (pack, again or "")
        joined = {}
        for part in parts.split(", "):
            member = part.split(" ", 1)[0]
            joined[member] = not part.endswith(" did not join")
            self._member(member)
        self.fights.append(Fight(int(number), name, joined))
        self._event(clock, "fight", "fight %s %s: %s" % (number, name, parts))
        for member, took_part in joined.items():
            if not took_part:
                self._event(clock, "did_not_join", "%s did not join fight %s %s" % (member, number, name))

    def feed_trace(self, line):
        match = TRACE.match(line.rstrip("\r\n"))
        if not match:
            return
        self.seconds = float(match.group(1))
        member = TRACE_MEMBER.match(match.group(2))
        if not member:
            return
        name, kind, rest = member.groups()
        if kind == "targets":
            self._member(name).target = "" if rest == "nothing" else rest
        elif kind == "casts":
            self._member(name).spell = rest.split(" ", 1)[1] if " " in rest else rest
        else:
            self._member(name)


class Pusher:
    """Pushes the wanted events, at most one of each kind per 30 s.
    `send(title, text)` does the push."""

    def __init__(self, send, clock=time.monotonic):
        self.send = send
        self.clock = clock
        self.last = {}

    def event(self, kind, text):
        if kind not in PUSHED_KINDS:
            return
        now = self.clock()
        if kind in self.last and now - self.last[kind] < PUSH_EVERY_SECONDS:
            return
        self.last[kind] = now
        try:
            self.send("WOWAI " + kind.replace("_", " "), text)
        except OSError as error:
            print("ntfy push failed: %s" % error, file=sys.stderr, flush=True)


def ntfy_sender(topic):
    def send(title, text):
        request = urllib.request.Request("https://ntfy.sh/" + topic, data=text.encode("utf-8"),
                                         headers={"Title": title})
        urllib.request.urlopen(request, timeout=10).close()
    return send


def page(state):
    """The HTML page of the state."""
    e = html.escape
    run = "Run %d" % state.runs_seen
    if state.count:
        run += " of %d" % state.count
    head = "%s: %s %s, run %d of mangosd" % (run, state.script, state.scenario, state.number) if state.number \
        else "No run yet"
    rows = []
    for name, member in state.members.items():
        ticks = "".join("&#10003;" if fight.joined.get(name) else "&#10007;" if name in fight.joined else "&middot;"
                        for fight in state.fights[-FIGHTS_SHOWN:])
        rows.append("<tr class=%s><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td class=t>%s</td></tr>" % (
            "a" if member.alive else "d", e(name), "alive" if member.alive else "dead", e(member.target),
            e(member.spell), ticks))
    events = "".join("<li class=%s>%s %s</li>" % (e(kind), e(clock), e(text))
                     for clock, kind, text in reversed(state.events[-EVENTS_SHOWN:]))
    end = ""
    if state.end:
        end = "<p class=end>%s</p>" % e(state.end)
        if state.threshold_failures:
            end += "<ul class=th>%s</ul>" % "".join("<li>threshold: %s</li>" % e(f) for f in state.threshold_failures)
    return """<!doctype html>
<html><head><meta charset="utf-8"><meta http-equiv="refresh" content="%d">
<meta name="viewport" content="width=device-width, initial-scale=1"><title>WOWAI run</title>
<style>
body{font-family:sans-serif;margin:8px;font-size:15px}table{border-collapse:collapse;width:100%%}
td{padding:3px 4px;border-bottom:1px solid #ddd}.d td{color:#b00}.t{letter-spacing:2px}
ul{padding-left:18px}li{margin:2px 0}.death,.wipe,.did_not_join,.run_end{color:#b00}.boss_kill{color:#070}
.end{font-weight:bold}.small{color:#666;font-size:13px}
</style></head><body>
<h3>%s</h3>
<p>%.0f s, %s<br>%s<br>pack %s, fights %d</p>
%s
<table><tr><th>member</th><th></th><th>target</th><th>last cast</th><th>last fights</th></tr>%s</table>
<h4>Events</h4><ul>%s</ul>
<p class=small>%s</p>
</body></html>
""" % (REFRESH_SECONDS, e(head), state.seconds, e(state.status or "phase: ask with --status"), e(state.line),
       e(state.pack or "-"), len(state.fights), end, "".join(rows), events, e(time.strftime("%H:%M:%S")))


class Tail:
    """The new lines of a file. A file that got shorter (a new mangosd start,
    a new run) is read again from its start."""

    def __init__(self, path):
        self.path = path
        self.offset = 0
        self.rest = ""

    def lines(self):
        try:
            size = os.path.getsize(self.path)
        except OSError:
            return []
        if size < self.offset:
            self.offset, self.rest = 0, ""
        with open(self.path, "rb") as f:
            f.seek(self.offset)
            data = f.read()
        self.offset += len(data)
        text = self.rest + data.decode("utf-8", "replace")
        lines = text.split("\n")
        self.rest = lines.pop()
        return lines


def lan_address():
    """The address of this PC on the home network. With WSL in mirrored
    networking mode, that of Windows."""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        try:
            s.connect(("192.0.2.1", 9))  # No packet goes out.
            return s.getsockname()[0]
        except OSError:
            return "127.0.0.1"


def threshold_failures(runs_dir, name):
    import runs
    try:
        with open(os.path.join(runs_dir, name)) as f:
            return runs.threshold_failures(json.load(f))
    except (OSError, ValueError, KeyError):
        return []


def watch(state, log, runs_dir, lock, stop):
    """Feeds the new lines of the log (a Tail) and of the live trace to the
    state, every second."""
    trace = Tail(os.path.join(runs_dir, LIVE_TRACE))
    while not stop.is_set():
        with lock:
            number = state.number
            for line in log.lines():
                state.feed_log(line)
            if state.number != number:
                # A new run replaces the live trace: read it from its start.
                trace = Tail(os.path.join(runs_dir, LIVE_TRACE))
            for line in trace.lines():
                state.feed_trace(line)
            if state.result_file and state.end and state.threshold_failures is None:
                state.threshold_failures = threshold_failures(runs_dir, state.result_file)
        stop.wait(READ_SECONDS)


def ask_status(realm, state, lock, stop):
    while not stop.is_set():
        try:
            answer = realm.soap(".wowai run status")
        except RuntimeError as error:
            answer = "status: %s" % error
        with lock:
            state.status = answer
        stop.wait(STATUS_SECONDS)


def serve(state, lock, port):
    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            with lock:
                body = page(state).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *args):
            pass

    return http.server.ThreadingHTTPServer(("0.0.0.0", port), Handler)


def main(argv=None):
    parser = argparse.ArgumentParser(description="Serve a phone page that follows the scripted runs of a realm.")
    realm_choice = parser.add_mutually_exclusive_group(required=True)
    realm_choice.add_argument("--ticket")
    realm_choice.add_argument("--shared", action="store_true")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--count", type=int)
    parser.add_argument("--ntfy")
    parser.add_argument("--runs-dir")
    parser.add_argument("--status", action="store_true")
    args = parser.parse_args(argv)

    from kit import Realm
    realm = Realm("shared" if args.shared else args.ticket)
    log_path = os.path.join(realm.dir, "storage", "mangosd", "logs", "Server.log")
    runs_dir = args.runs_dir or os.path.join(realm.dir, "storage", "mangosd", "logs", "runs")

    state = RunState()
    state.count = args.count
    lock = threading.Lock()
    # The lines that are in the log already only fill the page: no pushes.
    log = Tail(log_path)
    for line in log.lines():
        state.feed_log(line)
    state.runs_seen = 1 if state.number and not state.end else 0
    if args.ntfy:
        state.on_event = Pusher(ntfy_sender(args.ntfy)).event

    stop = threading.Event()
    threads = [threading.Thread(target=watch, args=(state, log, runs_dir, lock, stop), daemon=True)]
    if args.status:
        threads.append(threading.Thread(target=ask_status, args=(realm, state, lock, stop), daemon=True))
    server = serve(state, lock, args.port)
    for thread in threads:
        thread.start()
    print("Watch the runs at http://%s:%d/ (home Wi-Fi)%s" % (
        lan_address(), args.port, ", pushes to ntfy topic " + args.ntfy if args.ntfy else ""), flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()
        server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
