#!/usr/bin/env python3
"""The scripted runs over SOAP (spec section 10, Results).

    wowai/test-realm/runs.py --ticket <N> full [<count>]     <count> full runs (default 3)
    wowai/test-realm/runs.py --ticket <N> scenario <name> [<count>]   <count> runs of a scenario (default 1)

Options:
    --shared            The shared test realm instead of the realm of a ticket.
    --script <name>     The script file, bots/data/bottests/<name>.txt.
                        Default: ragefire_chasm.
    --noquests          Full runs without the quest steps (scenario
                        full_noquests).
    --out <folder>      Where the result files go. Default: runs-<time>/ in
                        the current folder.
    --runs-dir <folder> The host folder of Bots.Run.ResultDir, when the realm
                        sets one. Default: <realm folder>/storage/mangosd/logs/runs.
    --watch [<port>]    Also start the watch script (watch.py) on the port
                        (default 8098), to follow the runs on a phone. It
                        prints the address to open, and stops 10 minutes
                        after the last run (Ctrl+C stops it at once).
    --ntfy <topic>      With --watch: push the main events to ntfy.sh.

The realm must be up (`realm.sh --ticket <N> acquire`). For each run, the
script starts it with `.wowai run start`, asks `.wowai run status` every 10 s
until the run ends, and stops a run that takes too long. mangosd writes each
result file, and the trace of a run that did not pass, to the folder runs/ of
its logs, which the realm mounts at <realm folder>/storage/mangosd/logs/runs.
The script copies them from there to the out folder and prints one line per
run, and one line per pull: its threat losses, the healer's, and whether it
was clean. At the end it prints the measurement over all first pulls
(jvbastel/WOWAI#118). A full run passes when the run passed and its result file meets the
pass thresholds of spec section 10 (threshold_failures). After full runs it
says whether they make a scripted pass: 3 full runs in a row that pass
(PASSES_IN_A_ROW). It ends with exit code 0 when every run passed, or when
the full runs make a scripted pass.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "check"))

POLL_SECONDS = 10
# The longest wait for one run: the 70 minutes of a full run, plus the reset
# and the wait for the chat of the run. A scenario has its own limits.
FULL_RUN_SECONDS = 75 * 60
SCENARIO_SECONDS = 15 * 60

OVER = re.compile(r"^Run \d+ \S+ \S+: (passed|failed|aborted) after ", re.M)
RESULT = re.compile(r"Result: (\S+\.json)")
# The trace of the whole run that mangosd writes as it goes (BotRun), next
# to the result files. The next run starts it empty.
LIVE_TRACE = "live.trace.txt"


def run_outcome(status):
    """passed, failed, or aborted when the status line of `.wowai run status`
    says that the run is over; None while it runs."""
    match = OVER.search(status)
    return match.group(1) if match else None


def result_file(status):
    """The name of the result file in the status line of an ended run, or None."""
    match = RESULT.search(status)
    return match.group(1) if match else None


def summary(result):
    """One line for a result file (a dict)."""
    deaths = sum(len(times) for times in result["deaths"].values())
    line = "%s %s run %d: %s after %.0f s, bosses %d, wipes %d, deaths %d, threat losses %d" % (
        result["script"], result["scenario"], result["run"], result["result"], result["duration_s"],
        len(result["bosses_killed"]), len(result["wipes"]), deaths, len(result["threat_losses"]))
    # The healer's share (jvbastel/WOWAI#113). A result file of an older core has none.
    if "healer_threat_losses" in result:
        line += " (healer %d)" % result["healer_threat_losses"]
    line += ", lost mobs %d" % len(result["lost_mobs"])
    # The packs that joined a pull besides its own (jvbastel/WOWAI#103).
    extra = [pack for pull in result.get("pulls", []) for pack in pull.get("extra_packs", [])]
    line += ", extra packs %d" % len(extra)
    if extra:
        line += " (warned %d" % sum(1 for pack in extra if pack["warned"])
        # The judge's mark of each pack (jvbastel/WOWAI#123). An older result file has none.
        marks = ["%s %s" % (pack["pack"], "avoidable" if pack["avoidable"] else "unavoidable")
                 for pack in extra if "avoidable" in pack]
        if marks:
            line += "; " + ", ".join(marks)
        line += ")"
    # A pull with an unwarned or avoidable extra pack fails (jvbastel/WOWAI#123).
    # It is counted and shown here, but it does not fail the run (user decision).
    failed = [(i + 1, pull["failure"]) for i, pull in enumerate(result.get("pulls", []))
              if pull.get("passed") is False]
    if failed:
        line += ", failed pulls %d (%s)" % (len(failed), "; ".join("pull %d: %s" % pull for pull in failed))
    if result["reason"]:
        line += ": " + result["reason"]
    return line


# The pass thresholds of a full run (spec section 10). They can be tuned
# after the first runs.
BOSSES = 4
MAX_WIPES = 1
MIN_CLEAN_PULLS = 0.8
MAX_BOT_DEATHS = 2
MIN_HEALS_INTERRUPTED = 0.7
MAX_FIRE_NOVA_HITS = 1
# The roles (the result file's role names) that Fire Nova must not hit.
FIRE_NOVA_ROLES = ("ranged", "healer")
# The events that need a delivered line, and the events of the game (not the
# talk with the player).
EVENTS_WITH_A_LINE = ("boss_kill", "wipe", "loot_roll")
NO_GAME_EVENTS = ("player_message", "session_end")
# A scripted pass needs this many full runs in a row that pass.
PASSES_IN_A_ROW = 3


def run_plan(kind, arg, count, noquests):
    """The scenario, the number of runs, and the time limit of one run:
    `full [<count>]` (default 3) or `scenario <name> [<count>]` (default 1)."""
    if kind == "full":
        return ("full_noquests" if noquests else "full"), int(arg or 3), FULL_RUN_SECONDS
    return arg, int(count or 1), SCENARIO_SECONDS


def percent(share):
    return "%d%%" % round(share * 100)


def bot_deaths_outside_wipe(result):
    """The deaths of the bots (not the test driver) outside the one allowed
    wipe: in the first wipe, the last death of each member up to its time."""
    in_wipe = set()
    if result["wipes"]:
        wipe = result["wipes"][0]
        for member, times in result["deaths"].items():
            before = [t for t in times if t <= wipe]
            if before:
                in_wipe.add((member, max(before)))
    return [(member, t) for member, times in result["deaths"].items() if member != result["driver"]
            for t in times if (member, t) not in in_wipe]


def clean_pull_share(result):
    """The share of first pulls with no threat loss (see pull_reports). A
    fight again of a pack that lived after its fight (`again`) takes its own
    losses, but is no first pull."""
    first = [report for report in pull_reports(result) if not report["again"]]
    if not first:
        return 1.0
    return sum(1 for report in first if report["clean"]) / len(first)


def pull_reports(result):
    """Each pull of a result file, in time order (jvbastel/WOWAI#118): its
    threat losses, those on a healer, and whether it was clean (no threat
    loss). A loss belongs to the last pull before it, and a loss before any
    pull to the first pull that is no fight again. `again` marks a fight again of a pack, which is no
    first pull."""
    pulls = sorted(result.get("pulls", []), key=lambda pull: pull["time_s"])
    # The result file says which loss was on a healer. Result files from
    # before that flag name the healers only.
    healers = set(result.get("healers", []))
    reports = [{"pull": i + 1, "pack": pull.get("pack"), "target": pull.get("target", ""),
                "again": pull.get("again", False), "threat_losses": 0, "healer_threat_losses": 0}
               for i, pull in enumerate(pulls)]
    first = [i for i, report in enumerate(reports) if not report["again"]]
    for loss in result.get("threat_losses", []) if reports else []:
        before = [i for i, pull in enumerate(pulls) if pull["time_s"] <= loss["time_s"]]
        report = reports[before[-1] if before else (first or [0])[0]]
        report["threat_losses"] += 1
        report["healer_threat_losses"] += loss.get("healer", loss["member"] in healers)
    for report in reports:
        report["clean"] = report["threat_losses"] == 0
    return reports


def pull_line(report):
    """One line for a pull report."""
    name = ", ".join(part for part in (report["pack"], report["target"]) if part)
    return "pull %d%s%s: threat losses %d (healer %d), %s" % (
        report["pull"], " (%s)" % name if name else "", " again" if report["again"] else "",
        report["threat_losses"], report["healer_threat_losses"], "clean" if report["clean"] else "not clean")


def measurement(reports):
    """The measurement over the first pulls of the reports (one or more
    runs): the healer's threat losses per pull, the share of pulls with no
    healer threat loss, and the share of clean pulls. None with no pull."""
    first = [report for report in reports if not report["again"]]
    if not first:
        return None
    healer = sum(report["healer_threat_losses"] for report in first)
    return {
        "pulls": len(first),
        "healer_threat_losses": healer,
        "healer_threat_loss_rate": healer / len(first),
        "no_healer_threat_loss_share": sum(1 for r in first if r["healer_threat_losses"] == 0) / len(first),
        "clean_share": sum(1 for r in first if r["clean"]) / len(first),
    }


def measurement_line(m):
    return ("Measurement over %d pulls: healer threat losses %d (%.2f per pull), "
            "pulls with no healer threat loss %s, clean pulls %s" % (
                m["pulls"], m["healer_threat_losses"], m["healer_threat_loss_rate"],
                percent(m["no_healer_threat_loss_share"]), percent(m["clean_share"])))


def chat_failures(result):
    failures = []
    events = result["chat_events"]
    for event in events:
        if not event["reached"]:
            failures.append("%s at %.0f s did not reach the chat service" % (event["event"], event["time_s"]))
        if event["event"] in EVENTS_WITH_A_LINE and not event["lines"]:
            failures.append("%s at %.0f s got no line" % (event["event"], event["time_s"]))
    speakers = {line["bot"] for event in events if event["event"] not in NO_GAME_EVENTS for line in event["lines"]}
    for bot in result["group"]:
        if bot not in speakers:
            failures.append("no line for a game event from " + bot)
    messages = [event for event in events if event["event"] == "player_message"]
    for bot in result["group"]:
        whispers = [event for event in messages if event["to"] == bot]
        if len(whispers) != 1:
            failures.append("the test driver whispered %s %d times, once expected" % (bot, len(whispers)))
        for whisper in whispers:
            if not any(line["bot"] == bot for line in whisper["lines"]):
                failures.append("the whisper to %s got no answer from %s" % (bot, bot))
    party = [event for event in messages if event["to"] is None]
    if len(party) != 1:
        failures.append("the test driver wrote %d party messages, once expected" % len(party))
    for message in party:
        if not 1 <= len(message["lines"]) <= 2:
            failures.append("the party message got %d answers, 1 or 2 expected" % len(message["lines"]))
    return failures


def threshold_failures(result):
    """The pass thresholds of spec section 10 that a full run misses, one
    line each. A scenario has no thresholds: an empty list."""
    if not result["full_run"]:
        return []
    failures = []
    if result["result"] != "passed":
        failures.append("the run failed: %s" % result["reason"])
    if len(result["bosses_killed"]) < BOSSES:
        failures.append("bosses killed %d of %d" % (len(result["bosses_killed"]), BOSSES))
    if len(result["wipes"]) > MAX_WIPES:
        failures.append("wipes %d, at most %d" % (len(result["wipes"]), MAX_WIPES))
    clean = clean_pull_share(result)
    if clean < MIN_CLEAN_PULLS:
        failures.append("pulls with no threat loss %s, at least %s" % (percent(clean), percent(MIN_CLEAN_PULLS)))
    deaths = bot_deaths_outside_wipe(result)
    if len(deaths) > MAX_BOT_DEATHS:
        failures.append("bot deaths outside the wipe %d, at most %d" % (len(deaths), MAX_BOT_DEATHS))
    if result["double_interrupts"]:
        failures.append("double interrupts %d, none allowed" % len(result["double_interrupts"]))
    done = sum(1 for i in result["interrupts"] if i["kind"] == "heal")
    missed = sum(1 for m in result["missed_interrupts"] if m["kind"] == "heal")
    if done + missed and done / (done + missed) < MIN_HEALS_INTERRUPTED:
        failures.append("heals interrupted %s, at least %s" % (percent(done / (done + missed)),
                                                              percent(MIN_HEALS_INTERRUPTED)))
    if result["chases"]:
        failures.append("chases more than %d yards from the tank %d (%s)" % (
            result["chase_limit_yards"], len(result["chases"]),
            ", ".join("%s %.0f yd" % (c["member"], c["max_yards"]) for c in result["chases"])))
    fights = {}
    for hit in result["fire_nova_hits"]:
        if hit["role"] in FIRE_NOVA_ROLES:
            fights[hit["fight"]] = fights.get(hit["fight"], 0) + 1
    for fight, hits in sorted(fights.items()):
        if hits > MAX_FIRE_NOVA_HITS:
            failures.append("Fire Nova hits on ranged and healer in fight %d: %d, at most %d" % (
                fight, hits, MAX_FIRE_NOVA_HITS))
    return failures + chat_failures(result)


def longest_streak(passes):
    """The most runs in a row that passed. `passes`: one bool per run."""
    longest = streak = 0
    for passed in passes:
        streak = streak + 1 if passed else 0
        longest = max(longest, streak)
    return longest


def scripted_pass(passes):
    """True when PASSES_IN_A_ROW full runs in a row passed (spec section 10)."""
    return longest_streak(passes) >= PASSES_IN_A_ROW


def scripted_pass_line(passes):
    if scripted_pass(passes):
        return "Scripted pass: %d full runs in a row passed." % PASSES_IN_A_ROW
    return "No scripted pass: at most %d full runs in a row passed, %d needed." % (
        longest_streak(passes), PASSES_IN_A_ROW)


def wait_for_end(realm, limit_seconds):
    """The status line once the run is over. Stops the run after the limit."""
    end = time.time() + limit_seconds
    stopped = False
    while True:
        time.sleep(POLL_SECONDS)
        status = realm.soap(".wowai run status")
        if run_outcome(status):
            return status
        if not stopped and time.time() >= end:
            print("The run takes longer than %d s: stop it." % limit_seconds, flush=True)
            realm.soap(".wowai run stop")
            stopped = True


def collect(runs, status, out):
    """Copies the result file of the status line, its trace, and the live
    trace of the run, from the folder `runs` to `out`. The result as a dict, or None."""
    name = result_file(status)
    if not name:
        return None
    with open(os.path.join(runs, name)) as f:
        result = json.load(f)
    shutil.copy(os.path.join(runs, name), out)
    if result["trace"]:
        shutil.copy(os.path.join(runs, result["trace"]), out)
    # The whole run, also of a passed run (jvbastel/WOWAI#119).
    live = os.path.join(runs, LIVE_TRACE)
    if os.path.exists(live):
        shutil.copy(live, os.path.join(out, name[:-len(".json")] + ".live.trace.txt"))
    return result


def watch_port_argv(argv):
    """`--watch` with no port before the positional arguments: argparse
    would take `full` as the port. Gives it the default port."""
    argv = list(argv)
    for i, arg in enumerate(argv):
        if arg == "--watch" and (i + 1 == len(argv) or not argv[i + 1].isdigit()):
            argv[i] = "--watch=8098"
    return argv


def watch_command(realm_args, port, count, runs_dir, ntfy):
    """The command line of the watch script for the realm of `realm_args`
    (["--ticket", N] or ["--shared"])."""
    command = [sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), "watch.py")] + realm_args
    command += ["--port", str(port), "--count", str(count), "--runs-dir", runs_dir, "--status"]
    if ntfy:
        command += ["--ntfy", ntfy]
    return command


def stop_watch(watcher):
    """Stops the watch page right after the last run."""
    watcher.terminate()
    watcher.wait()


def main():
    parser = argparse.ArgumentParser(description="Start scripted runs over SOAP and collect their result files.")
    realm_choice = parser.add_mutually_exclusive_group(required=True)
    realm_choice.add_argument("--ticket")
    realm_choice.add_argument("--shared", action="store_true")
    parser.add_argument("--script", default="ragefire_chasm")
    parser.add_argument("--noquests", action="store_true")
    parser.add_argument("--out")
    parser.add_argument("--runs-dir")
    parser.add_argument("--watch", nargs="?", type=int, const=8098, metavar="PORT")
    parser.add_argument("--ntfy")
    parser.add_argument("kind", choices=["full", "scenario"])
    parser.add_argument("arg", nargs="?")
    parser.add_argument("count", nargs="?")
    args = parser.parse_args(watch_port_argv(sys.argv[1:]))

    if args.kind == "scenario" and not args.arg:
        parser.error("scenario needs a name")
    if args.kind == "full" and args.count:
        parser.error("full takes one count: full [<count>]")
    scenario, count, limit = run_plan(args.kind, args.arg, args.count, args.noquests)

    from kit import Realm
    realm = Realm("shared" if args.shared else args.ticket)
    out = args.out or "runs-" + time.strftime("%Y%m%d-%H%M%S")
    os.makedirs(out, exist_ok=True)
    runs_dir = args.runs_dir or os.path.join(realm.dir, "storage", "mangosd", "logs", "runs")
    watcher = None
    if args.watch:
        realm_args = ["--shared"] if args.shared else ["--ticket", args.ticket]
        watcher = subprocess.Popen(watch_command(realm_args, args.watch, count, runs_dir, args.ntfy),
                                   stdout=subprocess.PIPE, text=True)
        # Its first line (the address) comes once it read the log so far:
        # the start of the first run then gets its push.
        print(watcher.stdout.readline().rstrip(), flush=True)

    passes = []
    all_reports = []
    for n in range(1, count + 1):
        answer = realm.soap(".wowai run start %s %s" % (args.script, scenario))
        print("Run %d of %d: %s" % (n, count, answer), flush=True)
        if "running" not in answer:
            break
        status = wait_for_end(realm, limit)
        result = collect(runs_dir, status, out)
        if result is None:
            print("No result file: " + status, flush=True)
            passes.append(False)
            continue
        print(summary(result), flush=True)
        reports = pull_reports(result)
        for report in reports:
            print("  " + pull_line(report), flush=True)
        all_reports += reports
        failures = threshold_failures(result)
        for failure in failures:
            print("  threshold: " + failure, flush=True)
        passes.append(result["result"] == "passed" and not failures)

    m = measurement(all_reports)
    if m:
        print(measurement_line(m), flush=True)
    print("%d of %d runs passed. The result files are in %s." % (sum(passes), count, os.path.abspath(out)),
          flush=True)
    if args.kind == "full":
        print(scripted_pass_line(passes), flush=True)
    if watcher:
        stop_watch(watcher)
    every_run_passed = len(passes) == count and all(passes)
    return 0 if every_run_passed or (args.kind == "full" and scripted_pass(passes)) else 1


if __name__ == "__main__":
    sys.exit(main())
