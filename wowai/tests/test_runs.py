#!/usr/bin/env python3
"""Tests of the run script (wowai/test-realm/runs.py). No realm: the status
lines are those of BotRun::Status, and the result file that of
Bots::RunResultJson.

    python3 wowai/tests/test_runs.py
"""
import json
import os
import sys
import tempfile
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
                         "threat losses 1, lost mobs 0, extra packs 0: the second wipe")

    def test_counts_the_extra_packs_of_each_pull_and_the_warned_ones(self):
        """jvbastel/WOWAI#103: the packs that joined a pull, and the tank's warns."""
        result = {
            "run": 4, "script": "ragefire_chasm", "scenario": "two_packs", "result": "passed", "reason": None,
            "duration_s": 61.0, "bosses_killed": [], "wipes": [], "deaths": {"Ezzra": []},
            "threat_losses": [], "lost_mobs": [],
            "pulls": [{"pack": "pack48", "extra_packs": [{"pack": "pack49", "time_s": 30.0, "warned": False}]},
                      {"pack": "pack1", "extra_packs": [{"pack": "pack2", "time_s": 90.0, "warned": True},
                                                        {"pack": "pack3", "time_s": 91.0, "warned": False}]}],
        }
        self.assertEqual(runs.summary(result),
                         "ragefire_chasm two_packs run 4: passed after 61 s, bosses 0, wipes 0, deaths 0, "
                         "threat losses 0, lost mobs 0, extra packs 3 (warned 1)")

    def test_shows_the_threat_losses_of_the_healer(self):
        """jvbastel/WOWAI#113: the healer's threat losses next to all of them."""
        result = {
            "run": 5, "script": "ragefire_chasm", "scenario": "enforcers", "result": "passed", "reason": None,
            "duration_s": 80.0, "bosses_killed": [], "wipes": [], "deaths": {"Ezzra": []},
            "threat_losses": [{}, {}, {}], "healer_threat_losses": 2, "lost_mobs": [],
        }
        self.assertEqual(runs.summary(result),
                         "ragefire_chasm enforcers run 5: passed after 80 s, bosses 0, wipes 0, deaths 0, "
                         "threat losses 3 (healer 2), lost mobs 0, extra packs 0")


def a_three_enemy_run(losses):
    """A run of the three-enemy scenario (jvbastel/WOWAI#118): one pull of
    the miners at 30 s, and the threat losses (member, time_s) after it."""
    return {
        "run": 1, "script": "deadmines", "scenario": "three_enemies", "result": "passed", "reason": None,
        "healers": ["Hela"],
        "pulls": [{"pack": "miners", "target": "Defias Miner", "time_s": 30.0, "again": False, "extra_packs": []}],
        "threat_losses": [{"enemy": "Defias Miner", "member": member, "time_s": t, "seconds": 5.0}
                          for member, t in losses],
    }


class PullReport(unittest.TestCase):
    """jvbastel/WOWAI#118: each pull reports its threat losses, the healer's
    threat losses, and whether it was clean (no threat loss)."""

    def test_a_pull_with_no_threat_loss_is_clean(self):
        self.assertEqual(runs.pull_reports(a_three_enemy_run([])),
                         [{"pull": 1, "pack": "miners", "target": "Defias Miner", "again": False,
                           "threat_losses": 0, "healer_threat_losses": 0, "clean": True}])

    def test_counts_the_losses_of_the_pull_and_those_on_the_healer(self):
        report = runs.pull_reports(a_three_enemy_run([("Hela", 35.0), ("Ezzra", 36.0), ("Hela", 40.0)]))
        self.assertEqual([(r["threat_losses"], r["healer_threat_losses"], r["clean"]) for r in report],
                         [(3, 2, False)])

    def test_a_loss_belongs_to_the_last_pull_before_it(self):
        result = a_three_enemy_run([("Hela", 35.0), ("Shanka", 95.0)])
        result["pulls"].append({"pack": "pack2", "target": "Defias Miner", "time_s": 90.0, "again": False})
        report = runs.pull_reports(result)
        self.assertEqual([(r["pull"], r["pack"], r["threat_losses"], r["healer_threat_losses"]) for r in report],
                         [(1, "miners", 1, 1), (2, "pack2", 1, 0)])

    def test_a_loss_before_any_pull_belongs_to_the_first_pull_that_is_no_fight_again(self):
        result = a_three_enemy_run([("Hela", 5.0)])
        result["pulls"].insert(0, {"pack": "miners", "target": "Defias Miner", "time_s": 10.0, "again": True})
        report = runs.pull_reports(result)
        self.assertEqual([(r["again"], r["threat_losses"]) for r in report], [(True, 0), (False, 1)])
        self.assertEqual(runs.clean_pull_share(result), runs.measurement(report)["clean_share"])

    def test_a_result_with_no_threat_losses_field_has_clean_pulls(self):
        result = a_three_enemy_run([])
        del result["threat_losses"]
        self.assertTrue(runs.pull_reports(result)[0]["clean"])

    def test_one_line_per_pull(self):
        report = runs.pull_reports(a_three_enemy_run([("Hela", 35.0), ("Ezzra", 36.0)]))
        self.assertEqual(runs.pull_line(report[0]),
                         "pull 1 (miners, Defias Miner): threat losses 2 (healer 1), not clean")
        report = runs.pull_reports(a_three_enemy_run([]))
        self.assertEqual(runs.pull_line(report[0]),
                         "pull 1 (miners, Defias Miner): threat losses 0 (healer 0), clean")


class Measurement(unittest.TestCase):
    """jvbastel/WOWAI#118: the measurement over the first pulls of all runs
    (10 runs of the three-enemy scenario)."""

    def test_the_healer_rate_and_the_shares_of_the_pulls(self):
        reports = []
        for losses in ([], [("Hela", 35.0)], [("Hela", 35.0), ("Hela", 45.0)], [("Ezzra", 35.0)]):
            reports += runs.pull_reports(a_three_enemy_run(losses))
        m = runs.measurement(reports)
        self.assertEqual(m["pulls"], 4)
        self.assertEqual(m["healer_threat_losses"], 3)
        self.assertAlmostEqual(m["healer_threat_loss_rate"], 0.75)
        self.assertAlmostEqual(m["no_healer_threat_loss_share"], 0.5)
        self.assertAlmostEqual(m["clean_share"], 0.25)
        self.assertEqual(runs.measurement_line(m),
                         "Measurement over 4 pulls: healer threat losses 3 (0.75 per pull), "
                         "pulls with no healer threat loss 50%, clean pulls 25%")

    def test_a_fight_again_is_no_first_pull(self):
        result = a_three_enemy_run([("Hela", 95.0)])
        result["pulls"].append({"pack": "miners", "target": "Defias Miner", "time_s": 90.0, "again": True})
        m = runs.measurement(runs.pull_reports(result))
        self.assertEqual((m["pulls"], m["healer_threat_losses"], m["clean_share"]), (1, 0, 1.0))

    def test_no_pull_no_measurement(self):
        self.assertIsNone(runs.measurement([]))


class Plan(unittest.TestCase):
    def test_full_runs_default_to_three(self):
        self.assertEqual(runs.run_plan("full", None, None, False), ("full", 3, runs.FULL_RUN_SECONDS))
        self.assertEqual(runs.run_plan("full", "2", None, True), ("full_noquests", 2, runs.FULL_RUN_SECONDS))

    def test_a_scenario_runs_once_or_the_given_count(self):
        self.assertEqual(runs.run_plan("scenario", "two_packs", None, False),
                         ("two_packs", 1, runs.SCENARIO_SECONDS))
        self.assertEqual(runs.run_plan("scenario", "two_packs", "10", False),
                         ("two_packs", 10, runs.SCENARIO_SECONDS))


def a_full_run_that_passes():
    """A result file of a full run that meets every threshold of spec section 10."""
    group = ["Grukk", "Hela", "Shanka", "Morzul"]
    chat = [
        {"event": "boss_kill", "to": None, "time_s": 300.0, "reached": True,
         "lines": [{"bot": "Grukk", "time_s": 302.0}, {"bot": "Hela", "time_s": 303.0}]},
        {"event": "loot_roll", "to": None, "time_s": 310.0, "reached": True,
         "lines": [{"bot": "Shanka", "time_s": 312.0}]},
        {"event": "rest", "to": None, "time_s": 320.0, "reached": True,
         "lines": [{"bot": "Morzul", "time_s": 321.0}]},
    ]
    for bot in group:
        chat.append({"event": "player_message", "to": bot, "time_s": 25.0, "reached": True,
                     "lines": [{"bot": bot, "time_s": 27.0}]})
    chat.append({"event": "player_message", "to": None, "time_s": 26.0, "reached": True,
                 "lines": [{"bot": "Grukk", "time_s": 28.0}, {"bot": "Hela", "time_s": 29.0}]})
    return {
        "run": 1, "script": "ragefire_chasm", "scenario": "full", "full_run": True, "result": "passed",
        "reason": None, "duration_s": 1900.0, "driver": "Ezzra", "group": group,
        "bosses_killed": [{"entry": e, "name": "", "time_s": 300.0 * (i + 1)}
                          for i, e in enumerate([11517, 11518, 11519, 11520])],
        "wipes": [],
        "deaths": {"Ezzra": [], "Grukk": [], "Hela": [500.0], "Shanka": [], "Morzul": []},
        "pulls": [{"pack": "pack%d" % i, "target": "", "time_s": 100.0 * i, "again": False} for i in range(1, 11)],
        "threat_losses": [{"enemy": "Earthborer", "member": "Hela", "time_s": 105.0, "seconds": 5.0}],
        "lost_mobs": [],
        "interrupts": [{"member": "Shanka", "enemy": "Cultist", "spell": 332, "spell_name": "Healing Wave",
                        "kind": "heal", "time_s": 200.0}] * 7,
        "missed_interrupts": [{"enemy": "Cultist", "spell": 332, "spell_name": "Healing Wave", "kind": "heal",
                               "time_s": 210.0}] * 3,
        "double_interrupts": [],
        "chases": [],
        "chase_limit_yards": 25.0,
        "fire_nova_hits": [{"member": "Hela", "role": "healer", "fight": 1, "damage": 80, "time_s": 1200.0},
                           {"member": "Grukk", "role": "tank", "fight": 1, "damage": 80, "time_s": 1201.0}],
        "chat_events": chat,
    }


class Thresholds(unittest.TestCase):
    """The pass thresholds of a full run, spec section 10."""

    def fails(self, result):
        return runs.threshold_failures(result)

    def test_a_good_full_run_meets_every_threshold(self):
        self.assertEqual(self.fails(a_full_run_that_passes()), [])

    def test_a_failed_run_fails(self):
        result = a_full_run_that_passes()
        result["result"] = "failed"
        result["reason"] = "after 45 minutes"
        self.assertEqual(self.fails(result), ["the run failed: after 45 minutes"])

    def test_all_four_bosses_with_at_most_one_wipe(self):
        result = a_full_run_that_passes()
        result["bosses_killed"] = result["bosses_killed"][:3]
        self.assertEqual(self.fails(result), ["bosses killed 3 of 4"])
        result = a_full_run_that_passes()
        result["wipes"] = [600.0, 1200.0]
        result["deaths"] = {name: [599.0, 1199.0] for name in result["deaths"]}
        self.assertIn("wipes 2, at most 1", self.fails(result))

    def test_at_least_80_percent_of_pulls_have_no_threat_loss(self):
        result = a_full_run_that_passes()
        # A loss belongs to the last pull before it: pulls 1 and 2 lose, 8 of 10 are clean.
        result["threat_losses"].append({"enemy": "Earthborer", "member": "Hela", "time_s": 250.0, "seconds": 5.0})
        result["threat_losses"].append({"enemy": "Earthborer", "member": "Hela", "time_s": 255.0, "seconds": 5.0})
        self.assertEqual(self.fails(result), [])
        result["threat_losses"].append({"enemy": "Earthborer", "member": "Hela", "time_s": 350.0, "seconds": 5.0})
        self.assertEqual(self.fails(result), ["pulls with no threat loss 70%, at least 80%"])

    def test_only_first_pulls_count_for_the_clean_pull_share(self):
        result = a_full_run_that_passes()
        # Pulls 1 and 2 lose threat: 8 of 10 first pulls are clean.
        result["threat_losses"].append({"enemy": "Earthborer", "member": "Hela", "time_s": 250.0, "seconds": 5.0})
        # A fight again of pack 5 that loses threat: no first pull, it does not count.
        result["pulls"].append({"pack": "pack5", "target": "", "time_s": 550.0, "again": True})
        result["threat_losses"].append({"enemy": "Earthborer", "member": "Hela", "time_s": 555.0, "seconds": 5.0})
        self.assertEqual(self.fails(result), [])

    def test_at_most_two_bots_die_outside_the_one_allowed_wipe(self):
        result = a_full_run_that_passes()
        # The wipe at 600: each member's last death up to it is part of the wipe.
        result["wipes"] = [600.0]
        result["deaths"] = {"Ezzra": [599.0], "Grukk": [100.0, 598.0], "Hela": [500.0, 597.0],
                            "Shanka": [596.0], "Morzul": [595.0, 700.0]}
        self.assertEqual(self.fails(result), ["bot deaths outside the wipe 3, at most 2"])
        # The test driver is no bot.
        result["deaths"]["Morzul"] = [595.0]
        result["deaths"]["Ezzra"] = [50.0, 599.0]
        self.assertEqual(self.fails(result), [])

    def test_no_double_interrupt(self):
        result = a_full_run_that_passes()
        result["double_interrupts"] = [{"enemy": "Cultist", "spell": 332, "spell_name": "Healing Wave",
                                        "first": "Shanka", "second": "Grukk", "time_s": 200.5}]
        self.assertEqual(self.fails(result), ["double interrupts 1, none allowed"])

    def test_the_bots_interrupt_at_least_70_percent_of_the_heals(self):
        result = a_full_run_that_passes()
        result["missed_interrupts"].append(dict(result["missed_interrupts"][0]))
        self.assertEqual(self.fails(result), ["heals interrupted 64%, at least 70%"])
        # A missed harmful cast does not count.
        result = a_full_run_that_passes()
        result["missed_interrupts"].append(dict(result["missed_interrupts"][0], kind="harm"))
        self.assertEqual(self.fails(result), [])

    def test_no_chase_beyond_the_limit(self):
        result = a_full_run_that_passes()
        result["chases"] = [{"member": "Shanka", "enemy": "Earthborer", "time_s": 80.0, "seconds": 2.5,
                             "max_yards": 31.2}]
        self.assertEqual(self.fails(result), ["chases more than 25 yards from the tank 1 (Shanka 31 yd)"])
        # The limit comes from the result file, the one of the bots.
        result["chase_limit_yards"] = 30.0
        self.assertEqual(self.fails(result), ["chases more than 30 yards from the tank 1 (Shanka 31 yd)"])

    def test_at_most_one_fire_nova_hit_on_ranged_and_healer_per_fight(self):
        result = a_full_run_that_passes()
        result["fire_nova_hits"].append({"member": "Ezzra", "role": "ranged", "fight": 1, "damage": 80,
                                         "time_s": 1210.0})
        self.assertEqual(self.fails(result), ["Fire Nova hits on ranged and healer in fight 1: 2, at most 1"])
        result["fire_nova_hits"][-1]["fight"] = 2
        self.assertEqual(self.fails(result), [])

    def test_each_boss_kill_wipe_and_loot_roll_gets_a_line(self):
        result = a_full_run_that_passes()
        result["chat_events"].append({"event": "wipe", "to": None, "time_s": 600.0, "reached": True, "lines": []})
        self.assertEqual(self.fails(result), ["wipe at 600 s got no line"])

    def test_each_event_reaches_the_chat_service(self):
        result = a_full_run_that_passes()
        result["chat_events"][2]["reached"] = False
        self.assertEqual(self.fails(result), ["rest at 320 s did not reach the chat service"])

    def test_each_bot_says_a_line_for_a_game_event(self):
        result = a_full_run_that_passes()
        result["chat_events"][2]["lines"] = []
        self.assertEqual(self.fails(result), ["no line for a game event from Morzul"])

    def test_each_whisper_gets_an_answer_of_its_bot(self):
        result = a_full_run_that_passes()
        whisper = [e for e in result["chat_events"] if e["to"] == "Hela"][0]
        whisper["lines"] = [{"bot": "Grukk", "time_s": 27.0}]
        self.assertEqual(self.fails(result), ["the whisper to Hela got no answer from Hela"])
        result["chat_events"].remove(whisper)
        self.assertEqual(self.fails(result), ["the test driver whispered Hela 0 times, once expected"])

    def test_the_party_message_gets_one_or_two_answers(self):
        result = a_full_run_that_passes()
        party = [e for e in result["chat_events"] if e["event"] == "player_message" and e["to"] is None][0]
        party["lines"] = []
        self.assertEqual(self.fails(result), ["the party message got 0 answers, 1 or 2 expected"])
        result["chat_events"].remove(party)
        self.assertEqual(self.fails(result), ["the test driver wrote 0 party messages, once expected"])

    def test_a_scenario_has_no_thresholds(self):
        result = a_full_run_that_passes()
        result["full_run"] = False
        result["bosses_killed"] = []
        self.assertEqual(self.fails(result), [])


class Watch(unittest.TestCase):
    def test_the_watch_script_watches_the_same_realm_and_runs_folder(self):
        command = runs.watch_command(["--ticket", "98"], 8098, 3, "runs-x", None)

        self.assertTrue(command[1].endswith("watch.py"))
        self.assertEqual(command[2:], ["--ticket", "98", "--port", "8098", "--count", "3", "--runs-dir", "runs-x",
                                       "--status"])

    def test_watch_takes_a_port_only_when_one_follows(self):
        self.assertEqual(runs.watch_port_argv(["--ticket", "98", "--watch", "full", "3"]),
                         ["--ticket", "98", "--watch=8098", "full", "3"])
        self.assertEqual(runs.watch_port_argv(["--watch", "8198", "full"]), ["--watch", "8198", "full"])
        self.assertEqual(runs.watch_port_argv(["full", "--watch"]), ["full", "--watch=8098"])

    def test_passes_the_ntfy_topic_on(self):
        command = runs.watch_command(["--shared"], 9000, 1, "runs-x", "wowai-7f3k")

        self.assertEqual(command[-2:], ["--ntfy", "wowai-7f3k"])


class Collect(unittest.TestCase):
    """jvbastel/WOWAI#119: the trace of each run, also of a passed one, to
    see what the tank did."""

    def collect(self, trace):
        runs_dir = tempfile.mkdtemp()
        out = tempfile.mkdtemp()
        name = "shadowfang_keep-three_enemies-20261027-150000.json"
        with open(os.path.join(runs_dir, name), "w") as f:
            json.dump({"result": "failed" if trace else "passed", "trace": trace}, f)
        if trace:
            with open(os.path.join(runs_dir, trace), "w") as f:
                f.write("61.4 the last 3 minutes\n")
        with open(os.path.join(runs_dir, runs.LIVE_TRACE), "w") as f:
            f.write("0.0 run 7 starts\n")
        runs.collect(runs_dir, "Run 7 x y: passed after 9 s. Result: " + name, out)
        return sorted(os.listdir(out))

    def test_keeps_the_live_trace_of_a_passed_run(self):
        self.assertEqual(self.collect(""), ["shadowfang_keep-three_enemies-20261027-150000.json",
                                            "shadowfang_keep-three_enemies-20261027-150000.live.trace.txt"])

    def test_keeps_both_traces_of_a_failed_run(self):
        self.assertEqual(self.collect("shadowfang_keep-three_enemies-20261027-150000.trace.txt"),
                         ["shadowfang_keep-three_enemies-20261027-150000.json",
                          "shadowfang_keep-three_enemies-20261027-150000.live.trace.txt",
                          "shadowfang_keep-three_enemies-20261027-150000.trace.txt"])


if __name__ == "__main__":
    unittest.main()
