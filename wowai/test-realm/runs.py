#!/usr/bin/env python3
"""The scripted runs over SOAP (spec section 10, Results).

    wowai/test-realm/runs.py --ticket <N> full [<count>]     <count> full runs (default 3)
    wowai/test-realm/runs.py --ticket <N> scenario <name>    one scenario

Options:
    --shared            The shared test realm instead of the realm of a ticket.
    --script <name>     The script file, bots/data/bottests/<name>.txt.
                        Default: ragefire_chasm.
    --noquests          Full runs without the quest steps (scenario
                        full_noquests).
    --out <folder>      Where the result files go. Default: runs-<time>/ in
                        the current folder.

The realm must be up (`realm.sh --ticket <N> acquire`). For each run, the
script starts it with `.wowai run start`, asks `.wowai run status` every 10 s
until the run ends, and stops a run that takes too long. mangosd writes each
result file, and the trace of a run that did not pass, to the folder runs/ of
its logs, which the realm mounts at <realm folder>/storage/mangosd/logs/runs.
The script copies them from there to the out folder and prints one line per
run. It ends with exit code 0 when every run passed.
"""
import argparse
import json
import os
import re
import shutil
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "check"))

POLL_SECONDS = 10
# The longest wait for one run: the 45 minutes of a full run, plus the reset
# and the wait for the chat of the run. A scenario has its own limits.
FULL_RUN_SECONDS = 50 * 60
SCENARIO_SECONDS = 15 * 60

OVER = re.compile(r"^Run \d+ \S+ \S+: (passed|failed|aborted) after ", re.M)
RESULT = re.compile(r"Result: (\S+\.json)")


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
    line = "%s %s run %d: %s after %.0f s, bosses %d, wipes %d, deaths %d, threat losses %d, lost mobs %d" % (
        result["script"], result["scenario"], result["run"], result["result"], result["duration_s"],
        len(result["bosses_killed"]), len(result["wipes"]), deaths, len(result["threat_losses"]),
        len(result["lost_mobs"]))
    if result["reason"]:
        line += ": " + result["reason"]
    return line


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


def collect(realm_dir, status, out):
    """Copies the result file of the status line, and its trace, to `out`.
    The result as a dict, or None."""
    name = result_file(status)
    if not name:
        return None
    runs = os.path.join(realm_dir, "storage", "mangosd", "logs", "runs")
    with open(os.path.join(runs, name)) as f:
        result = json.load(f)
    shutil.copy(os.path.join(runs, name), out)
    if result["trace"]:
        shutil.copy(os.path.join(runs, result["trace"]), out)
    return result


def main():
    parser = argparse.ArgumentParser(description="Start scripted runs over SOAP and collect their result files.")
    realm_choice = parser.add_mutually_exclusive_group(required=True)
    realm_choice.add_argument("--ticket")
    realm_choice.add_argument("--shared", action="store_true")
    parser.add_argument("--script", default="ragefire_chasm")
    parser.add_argument("--noquests", action="store_true")
    parser.add_argument("--out")
    parser.add_argument("kind", choices=["full", "scenario"])
    parser.add_argument("arg", nargs="?")
    args = parser.parse_args()

    if args.kind == "full":
        scenario = "full_noquests" if args.noquests else "full"
        count = int(args.arg or 3)
        limit = FULL_RUN_SECONDS
    else:
        if not args.arg:
            parser.error("scenario needs a name")
        scenario, count, limit = args.arg, 1, SCENARIO_SECONDS

    from kit import Realm
    realm = Realm("shared" if args.shared else args.ticket)
    out = args.out or "runs-" + time.strftime("%Y%m%d-%H%M%S")
    os.makedirs(out, exist_ok=True)

    passed = 0
    for n in range(1, count + 1):
        answer = realm.soap(".wowai run start %s %s" % (args.script, scenario))
        print("Run %d of %d: %s" % (n, count, answer), flush=True)
        if "running" not in answer:
            break
        status = wait_for_end(realm, limit)
        result = collect(realm.dir, status, out)
        if result is None:
            print("No result file: " + status, flush=True)
            continue
        print(summary(result), flush=True)
        passed += result["result"] == "passed"

    print("%d of %d runs passed. The result files are in %s." % (passed, count, os.path.abspath(out)), flush=True)
    return 0 if passed == count else 1


if __name__ == "__main__":
    sys.exit(main())
