#!/usr/bin/env python3
"""Tests of the watch script (wowai/test-realm/watch.py). No realm: the log
lines are those of BotRun in Server.log, and the trace lines those of
Bots::RunTrace in live.trace.txt.

    python3 wowai/tests/test_watch.py
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "test-realm"))
import watch  # noqa: E402

START = "2026-10-26 17:04:00 [Bots] Run 3: ragefire_chasm full starts, test driver Ezzra"
RESET = ("2026-10-26 17:04:01 [Bots] Run 3: reset Grukk: level 16, xp 0, 0 bag items destroyed, health 400/400, "
         "mana 0/0. ok")
FIGHT = "2026-10-26 17:10:32 [Bots] Run 3: line 40: fight pack7"
SUMMARY = ("2026-10-26 17:10:54 [Bots] Run 3: fight 7 pack7: Ezzra 1.2 s 3 casts, Grukk 0.4 s 0 casts, "
           "Hela 2.0 s 1 cast, Morzul did not join")
FAILED = ("2026-10-26 17:12:36 [Bots] Run 3 ragefire_chasm full: failed after 351 s: lost mob: Ragefire Trogg on "
          "Hela, and no bot attacks it for more than 10 s, bosses 0, wipes 0, deaths 0, threat losses 0, lost mobs 1")


def state_of(*lines):
    state = watch.RunState()
    for line in lines:
        state.feed_log(line)
    return state


class Log(unittest.TestCase):
    def test_a_start_line_starts_a_new_run(self):
        state = state_of(START, "2026-10-26 17:04:05 [Bots] Run 3: line 17: teleport p1")

        self.assertEqual((state.number, state.script, state.scenario), (3, "ragefire_chasm", "full"))
        self.assertEqual(state.line, "line 17: teleport p1")
        self.assertEqual(state.runs_seen, 1)

        state.feed_log("2026-10-26 17:20:00 [Bots] Run 4: ragefire_chasm full starts, test driver Ezzra")
        self.assertEqual((state.number, state.line, state.runs_seen), (4, "", 2))

    def test_a_fight_line_names_the_pack_and_is_an_event(self):
        state = state_of(START, FIGHT)

        self.assertEqual(state.pack, "pack7")
        self.assertEqual(state.events[-1], ("17:10:32", "pull", "fight pack7"))

    def test_a_command_pull_is_a_pull_of_the_pack_that_the_driver_targets(self):
        state = state_of(START, "2026-10-26 17:10:30 [Bots] Run 3: line 115: target pack10",
                         "2026-10-26 17:10:31 [Bots] Run 3: line 116: command pull")

        self.assertEqual(state.pack, "pack10")
        self.assertEqual(state.events[-1], ("17:10:31", "pull", "command pull"))

    def test_a_death_and_a_revive_change_who_lives(self):
        state = state_of(START, RESET, "2026-10-26 17:05:00 [Bots] Run 3: Grukk dies")
        self.assertFalse(state.members["Grukk"].alive)
        self.assertEqual(state.events[-1][1], "death")

        state.feed_log("2026-10-26 17:05:30 [Bots] Run 3: Grukk revives at Hela")
        self.assertTrue(state.members["Grukk"].alive)

        state.feed_log("2026-10-26 17:06:00 [Bots] Run 3: Grukk dies")
        state.feed_log("2026-10-26 17:06:30 [Bots] Run 3: wipe: the test driver releases, and everyone revives "
                       "at the dungeon entrance")
        self.assertTrue(state.members["Grukk"].alive)

    def test_wipes_boss_kills_threat_losses_and_chases_are_events(self):
        state = state_of(START,
                         "2026-10-26 17:06:00 [Bots] Run 3: wipe 1",
                         "2026-10-26 17:07:00 [Bots] Run 3: boss_kill 11517 Oggleflint",
                         "2026-10-26 17:08:00 [Bots] Run 3: threat loss: Ragefire Trogg on Hela for more than 4 s",
                         "2026-10-26 17:09:00 [Bots] Run 3: Morzul chases Ragefire Trogg more than 25 yards from "
                         "the tank",
                         "2026-10-26 17:09:01 [Bots] Run 3: line 40: move p13")

        self.assertEqual([kind for _, kind, _ in state.events],
                         ["run_start", "wipe", "boss_kill", "threat_loss", "chase"])
        self.assertEqual(state.events[2][2], "boss_kill 11517 Oggleflint")

    def test_a_fight_summary_gives_each_member_a_tick_and_names_who_did_not_join(self):
        state = state_of(START, FIGHT, SUMMARY)

        fight = state.fights[-1]
        self.assertEqual((fight.number, fight.pack), (7, "pack7"))
        self.assertEqual(fight.joined, {"Ezzra": True, "Grukk": True, "Hela": True, "Morzul": False})
        self.assertEqual(state.events[-1], ("17:10:54", "did_not_join", "Morzul did not join fight 7 pack7"))
        self.assertIn("Morzul", state.members)

    def test_the_end_line_of_a_run(self):
        state = state_of(START, FAILED)

        self.assertEqual(state.outcome, "failed")
        self.assertTrue(state.end.startswith("Run 3 ragefire_chasm full: failed after 351 s: lost mob"))
        self.assertEqual(state.events[-1][1], "run_end")

    def test_the_end_lines_of_earlier_runs_stay(self):
        state = state_of(START, FAILED, "2026-10-26 17:20:00 [Bots] Run 4: ragefire_chasm full starts, test driver Ezzra")

        self.assertEqual(state.end, "")
        self.assertEqual(len(state.ends), 1)
        self.assertTrue(state.ends[0].line.startswith("Run 3 ragefire_chasm full: failed after 351 s"))
        self.assertIn("failed after 351 s", watch.page(state))

    def test_the_result_file_of_the_run(self):
        state = state_of(START, "2026-10-26 17:12:36 [Bots] Run 3: result /opt/vmangos/storage/logs/runs/"
                         "ragefire_chasm-full-20261026-130645.json, trace ragefire_chasm-full-20261026-130645.trace.txt")

        self.assertEqual(state.result_file, "ragefire_chasm-full-20261026-130645.json")

    def test_other_lines_change_nothing(self):
        state = state_of(START)
        before = list(state.events)
        state.feed_log("2026-10-26 17:09:01 [Bots] Hela: drink")
        state.feed_log("2026-10-26 17:09:01 Some other line")
        state.feed_log("2026-10-26 17:09:01 [Bots] Run 2: wipe 1")

        self.assertEqual(state.events, before)


class Trace(unittest.TestCase):
    def test_a_trace_line_gives_the_time_the_target_and_the_cast_of_a_member(self):
        state = state_of(START)
        state.feed_trace("61.4 Grukk at -4.94 -22.40 -20.20")
        state.feed_trace("62.0 Grukk targets Earthborer 38043")
        state.feed_trace("62.1 Hela casts 2054 Lesser Heal")

        self.assertEqual(state.seconds, 62.1)
        self.assertEqual(state.members["Grukk"].target, "Earthborer 38043")
        self.assertEqual(state.members["Hela"].spell, "Lesser Heal")

        state.feed_trace("63.0 Grukk targets nothing")
        self.assertEqual(state.members["Grukk"].target, "")

    def test_other_trace_lines_only_move_the_time(self):
        state = state_of(START)
        state.feed_trace("70.0 line 12: fight pack1")
        state.feed_trace("70.5 Ezzra says in party: pull")

        self.assertEqual(state.seconds, 70.5)
        self.assertNotIn("Ezzra says in party:", state.members)


class Pushes(unittest.TestCase):
    def test_pushes_the_wanted_events_at_most_once_a_kind_per_30_seconds(self):
        sent = []
        now = [1000.0]
        pusher = watch.Pusher(lambda title, text: sent.append((title, text)), clock=lambda: now[0])

        pusher.event("run_start", "Run 3 starts")
        pusher.event("pull", "fight pack7")
        pusher.event("wipe", "wipe 1")
        now[0] += 10
        pusher.event("wipe", "wipe 2")
        now[0] += 25
        pusher.event("wipe", "wipe 3")

        self.assertEqual([text for _, text in sent], ["Run 3 starts", "wipe 1", "wipe 3"])


class Page(unittest.TestCase):
    def test_the_page_shows_the_run_the_members_the_fights_and_the_events(self):
        state = state_of(START, RESET, FIGHT, SUMMARY, FAILED)
        state.count = 3
        state.feed_trace("62.0 Grukk targets Earthborer 38043")

        page = watch.page(state)

        for text in ("Run 1 of 3", "ragefire_chasm full", "pack7", "Grukk", "Earthborer 38043",
                     "Morzul did not join fight 7 pack7", "failed after 351 s"):
            self.assertIn(text, page)
        self.assertIn('http-equiv="refresh"', page)

    def test_the_page_escapes_names(self):
        state = state_of(START)
        state.feed_trace("62.0 Grukk targets <b>Boss</b> 1")

        self.assertIn("&lt;b&gt;Boss&lt;/b&gt;", watch.page(state))


if __name__ == "__main__":
    unittest.main()
