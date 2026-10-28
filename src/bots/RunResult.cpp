#include "RunResult.h"
#include "RunJudge.h"

#include <algorithm>

#include <nlohmann/json.hpp>

namespace Bots
{
    namespace
    {
        using Json = nlohmann::json;

        double Seconds(std::int64_t ms)
        {
            return double(ms) / 1000.0;
        }

        char const* OutcomeName(RunOutcome outcome)
        {
            switch (outcome)
            {
                case RunOutcome::Passed: return "passed";
                case RunOutcome::Aborted: return "aborted";
                default: return "failed";
            }
        }

        Json TextOrNull(std::string const& text)
        {
            return text.empty() ? Json() : Json(text);
        }

        bool OnHealer(RunResult const& r, RunHit const& loss)
        {
            return Contains(r.healers, loss.member);
        }

        Json Hits(std::vector<RunHit> const& hits)
        {
            Json list = Json::array();
            for (RunHit const& hit : hits)
                list.push_back({{"enemy", hit.enemy}, {"member", hit.member}, {"time_s", Seconds(hit.startMs)},
                    {"seconds", Seconds(hit.endMs - hit.startMs)}});
            return list;
        }

        char const* CastKindName(CastKind kind)
        {
            switch (kind)
            {
                case CastKind::Heal: return "heal";
                case CastKind::Harm: return "harm";
                case CastKind::Other: return "other";
                default: return "none";
            }
        }

        Json Objective(ObjectiveProgress const& progress)
        {
            QuestObjective const& o = progress.objective;
            Json j = {{"quest", progress.quest}};
            switch (o.kind)
            {
                case QuestObjectiveKind::Visit:
                    j["kind"] = "visit";
                    j["point"] = o.point;
                    break;
                case QuestObjectiveKind::Kill:
                    j["kind"] = "kill";
                    j["entry"] = o.entry;
                    break;
                case QuestObjectiveKind::Item:
                    j["kind"] = "item";
                    j["item"] = o.item;
                    break;
            }
            j["done"] = progress.done;
            j["needed"] = progress.Needed();
            j["complete"] = progress.Complete();
            return j;
        }
    }

    std::string RunFileStem(RunResult const& result)
    {
        return result.script + "-" + result.scenario + "-" + result.stamp;
    }

    std::size_t HealerThreatLosses(RunResult const& r)
    {
        return static_cast<std::size_t>(std::count_if(r.threatLosses.begin(), r.threatLosses.end(),
            [&](RunHit const& loss) { return OnHealer(r, loss); }));
    }

    std::string RunResultJson(RunResult const& r)
    {
        Json j;
        j["run"] = r.number;
        j["script"] = r.script;
        j["scenario"] = r.scenario;
        j["full_run"] = r.fullRun;
        j["quests"] = r.quests == RunQuests::Do;
        j["started_at"] = r.startedAt;
        j["duration_s"] = Seconds(r.durationMs);
        j["result"] = OutcomeName(r.outcome);
        j["reason"] = TextOrNull(r.reason);
        j["driver"] = r.driver;
        j["group"] = r.group;
        j["healers"] = r.healers;

        Json bosses = Json::array();
        for (RunBossKill const& kill : r.bossKills)
            bosses.push_back({{"entry", kill.entry}, {"name", kill.name}, {"time_s", Seconds(kill.ms)}});
        j["bosses_killed"] = bosses;

        Json wipes = Json::array();
        for (std::int64_t ms : r.wipes)
            wipes.push_back(Seconds(ms));
        j["wipes"] = wipes;

        // Each member, also one that never died.
        Json deaths = Json::object();
        deaths[r.driver] = Json::array();
        for (std::string const& member : r.group)
            deaths[member] = Json::array();
        for (RunDeath const& death : r.deaths)
            deaths[death.member].push_back(Seconds(death.ms));
        j["deaths"] = deaths;

        Json pulls = Json::array();
        std::size_t extraPacks = 0;
        std::size_t failedPulls = 0;
        std::size_t avoidable = 0;
        for (RunPull const& pull : r.pulls)
        {
            Json extra = Json::array();
            for (RunExtraPack const& pack : pull.extraPacks)
            {
                extra.push_back({{"pack", pack.pack}, {"time_s", Seconds(pack.ms)}, {"warned", pack.warned},
                    {"avoidable", pack.avoidable}, {"reason", pack.reason}});
                if (pack.avoidable)
                    ++avoidable;
            }
            extraPacks += pull.extraPacks.size();
            // The verdict of the pull (RunJudge.h, jvbastel/WOWAI#123).
            std::string const failure = PullFailure(pull);
            Json written = {{"pack", TextOrNull(pull.pack)}, {"target", pull.target}, {"time_s", Seconds(pull.ms)},
                {"again", pull.again}, {"extra_packs", extra}, {"passed", failure.empty()}};
            if (!failure.empty())
            {
                written["failure"] = failure;
                ++failedPulls;
            }
            pulls.push_back(written);
        }
        j["pulls"] = pulls;
        // Of all pulls: the score of the run counts each extra pack, warned,
        // avoidable or not.
        j["extra_packs"] = extraPacks;
        j["avoidable_extra_packs"] = avoidable;
        // A failed pull does not fail the run (user decision, 2026-10-27).
        j["failed_pulls"] = failedPulls;

        Json fights = Json::array();
        for (FightSummary const& fight : r.fights)
        {
            Json members = Json::object();
            for (FightMember const& member : fight.members)
                members[member.name] = {{"joined", member.joined},
                    {"first_action_s", member.joined ? Json(Seconds(member.firstActionMs)) : Json()},
                    {"casts", member.casts}};
            fights.push_back({{"pull", fight.number}, {"pack", TextOrNull(fight.pack)}, {"again", fight.again},
                {"time_s", Seconds(fight.startMs)}, {"seconds", Seconds(fight.endMs - fight.startMs)},
                {"members", members}});
        }
        j["fights"] = fights;
        // Each loss says whether it was on a healer, for the pull reports
        // of runs.py (jvbastel/WOWAI#118).
        Json losses = Hits(r.threatLosses);
        for (std::size_t i = 0; i < r.threatLosses.size(); ++i)
            losses[i]["healer"] = OnHealer(r, r.threatLosses[i]);
        j["threat_losses"] = losses;
        // Each member, also one with no loss, and the healer's count
        // (jvbastel/WOWAI#113).
        Json byMember = Json::object();
        byMember[r.driver] = 0;
        for (std::string const& member : r.group)
            byMember[member] = 0;
        for (RunHit const& loss : r.threatLosses)
            byMember[loss.member] = byMember.value(loss.member, 0) + 1;
        j["threat_losses_by_member"] = byMember;
        j["healer_threat_losses"] = HealerThreatLosses(r);
        j["lost_mobs"] = Hits(r.lostMobs);
        j["moves_stopped"] = r.movesStopped;

        Json interrupts = Json::array();
        for (RunInterrupt const& i : r.interrupts)
            interrupts.push_back({{"member", i.member}, {"enemy", i.enemy}, {"spell", i.spell},
                {"spell_name", i.spellName}, {"kind", CastKindName(i.kind)}, {"time_s", Seconds(i.ms)}});
        j["interrupts"] = interrupts;
        Json missed = Json::array();
        for (RunMissedCast const& m : r.missedCasts)
            missed.push_back({{"enemy", m.enemy}, {"spell", m.spell}, {"spell_name", m.spellName},
                {"kind", CastKindName(m.kind)}, {"time_s", Seconds(m.ms)}});
        j["missed_interrupts"] = missed;
        Json doubles = Json::array();
        for (RunDoubleInterrupt const& d : r.doubleInterrupts)
            doubles.push_back({{"enemy", d.enemy}, {"spell", d.spell}, {"spell_name", d.spellName},
                {"first", d.first}, {"second", d.second}, {"time_s", Seconds(d.ms)}});
        j["double_interrupts"] = doubles;

        Json chases = Json::array();
        for (RunChase const& c : r.chases)
            chases.push_back({{"member", c.member}, {"enemy", c.enemy}, {"time_s", Seconds(c.startMs)},
                {"seconds", Seconds(c.endMs - c.startMs)}, {"max_yards", c.maxYards}});
        j["chases"] = chases;
        j["chase_limit_yards"] = r.chaseLimitYards;
        Json novas = Json::array();
        for (RunFireNovaHit const& hit : r.fireNovaHits)
            novas.push_back({{"member", hit.member}, {"role", RoleName(hit.role)}, {"fight", hit.fight},
                {"damage", hit.damage}, {"time_s", Seconds(hit.ms)}});
        j["fire_nova_hits"] = novas;

        Json chat = Json::array();
        for (RunChatEvent const& event : r.chatEvents)
        {
            Json lines = Json::array();
            for (RunChatLine const& line : event.lines)
                lines.push_back({{"bot", line.bot}, {"time_s", Seconds(line.ms)}});
            chat.push_back({{"event", event.event}, {"to", TextOrNull(event.to)}, {"time_s", Seconds(event.ms)},
                {"reached", event.reached}, {"lines", lines}});
        }
        j["chat_events"] = chat;

        Json draws = Json::array();
        for (RunGearDraw const& draw : r.gearDraws)
        {
            Json items = Json::array();
            for (RunGearItem const& item : draw.items)
                items.push_back({{"position", item.position}, {"entry", item.entry}, {"name", item.name}});
            draws.push_back({{"member", draw.member}, {"template", draw.gearTemplate}, {"shape", draw.shape},
                {"items", items}});
        }
        j["gear_draws"] = draws;

        Json choices = Json::array();
        for (RunChoice const& choice : r.choices)
            choices.push_back({{"who", choice.who}, {"point", choice.point}, {"place", choice.place},
                {"places", choice.places}, {"x", choice.x}, {"y", choice.y}, {"time_s", Seconds(choice.ms)}});
        j["random_choices"] = choices;

        Json objectives = Json::array();
        for (ObjectiveProgress const& progress : r.questProgress)
            objectives.push_back(Objective(progress));
        j["quest_objectives"] = objectives;

        j["trace"] = TextOrNull(r.traceFile);
        return j.dump(2, ' ', false, Json::error_handler_t::replace) + "\n";
    }

    std::uint32_t FightNumber(std::vector<RunPull> const& pulls)
    {
        std::uint32_t fight = 1;
        for (std::size_t i = pulls.size(); i-- > 1 && pulls[i].again;)
            ++fight;
        return fight;
    }
}
