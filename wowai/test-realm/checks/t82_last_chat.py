#!/usr/bin/env python3
"""In-game check: the last boss_kill of a run gets its bot chat line (spec section 10), on the check kit.

    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t82_last_chat.py

The scenario `taragaman` ends at the kill of its boss. The run then holds
the run group until the chat of the run is said or dropped, and only then
the members leave the group (jvbastel/WOWAI#82). The check kit runs the chat
service with the fake LLM backend, so each event gets lines.
"""
import re

from kit import Realm, Results

BOSS = 11520  # Taragaman the Hungerer

realm = Realm()
results = Results()
c = realm.login("PLAYER82C", "Garthok")
c.gm(".gm on")


def run_status_until(pattern, seconds):
    for _ in range(seconds):
        status = realm.soap(".wowai run status")
        if re.search(pattern, status):
            return status
        c.pump(1)
    return realm.soap(".wowai run status")


realm.mark("taragaman")
print(" ", realm.soap(".wowai run start ragefire_chasm taragaman"))
status = run_status_until(r"passed|failed", 600)
results.check("the scenario taragaman passes", "passed" in status, status)
left = realm.wait_log(r"Run \d+: the members leave the run group", 30, c)
results.check("the members leave the run group", left)
c.pump(3)

log = realm.log()
kill = re.search(r"Group (\d+) kills the boss %d" % BOSS, log)
results.check("the log has the boss_kill of the run group", kill is not None)
if kill:
    group = kill.group(1)
    after = log[kill.end():]
    end = re.search(r"Run \d+: the members leave the run group %s" % group, after)
    before_end = after[:end.start()] if end else after
    said = re.findall(r"\[BotChat\] (\w+) says \(group %s\): (.*)" % group, before_end)
    results.check("a bot says a line of the group after the boss_kill, before the group ends", len(said) > 0,
                  "; ".join("%s: %s" % s for s in said) or "no line")
    dropped = re.findall(r"\[BotChat\] dropped a line of .*", after)
    results.check("no line of the run drops at the end of the run", not dropped, "; ".join(dropped))

results.finish()
