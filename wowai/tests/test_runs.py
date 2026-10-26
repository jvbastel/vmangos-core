#!/usr/bin/env python3
"""Tests of the run script (wowai/test-realm/runs.py). No realm: the status
lines are those of BotRun::Status, and the result file that of
Bots::RunResultJson.

    python3 wowai/tests/test_runs.py
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "test-realm"))
import runs  # noqa: E402

PASSED = ("Run 2 ragefire_chasm full: passed after 1934 s, bosses 4, wipes 0, deaths 1, threat losses 0, "
          "lost mobs 0. Result: ragefire_chasm-full-20261026-101500.json")
FAILED = ("Run 3 ragefire_chasm taragaman: failed after 212 s: the second wipe, bosses 0, wipes 2, deaths 10, "
          "threat losses 1 (Taragaman the Hungerer on Hela 5 s), lost mobs 0. "
          "Result: ragefire_chasm-taragaman-20261026-110000.json")
ABORTED = "Run 4 ragefire_chasm full: aborted after 30 s: stopped by a GM, bosses 0, wipes 0, deaths 0"
RUNNING = "Run 5 ragefire_chasm full: running, script, line 45: route (120 s)"
CLOSING = "Run 5 ragefire_chasm full: running, closing: the group waits for the chat of the run (1900 s)"


class RunOutcome(unittest.TestCase):
    def test_an_ended_run_says_how_it_ended(self):
        self.assertEqual(runs.run_outcome(PASSED), "passed")
        self.assertEqual(runs.run_outcome(FAILED), "failed")
        self.assertEqual(runs.run_outcome(ABORTED), "aborted")

    def test_a_run_that_runs_or_waits_for_its_chat_is_not_over(self):
        self.assertIsNone(runs.run_outcome(RUNNING))
        self.assertIsNone(runs.run_outcome(CLOSING))
        self.assertIsNone(runs.run_outcome("No run yet."))

    def test_finds_the_result_file_of_an_ended_run(self):
        self.assertEqual(runs.result_file(PASSED), "ragefire_chasm-full-20261026-101500.json")
        self.assertIsNone(runs.result_file(ABORTED))


class Summary(unittest.TestCase):
    def test_one_line_per_run(self):
        result = {
            "run": 3, "script": "ragefire_chasm", "scenario": "full", "result": "failed", "reason": "the second wipe",
            "duration_s": 1234.5, "bosses_killed": [{"entry": 11517}], "wipes": [600.0, 1200.0],
            "deaths": {"Ezzra": [], "Grukk": [600.0], "Hela": [590.0, 1199.0]},
            "threat_losses": [{}], "lost_mobs": [],
        }
        self.assertEqual(runs.summary(result),
                         "ragefire_chasm full run 3: failed after 1234 s, bosses 1, wipes 2, deaths 3, "
                         "threat losses 1, lost mobs 0: the second wipe")


if __name__ == "__main__":
    unittest.main()
