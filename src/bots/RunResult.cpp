#include "RunResult.h"

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

        Json Hits(std::vector<RunHit> const& hits)
        {
            Json list = Json::array();
            for (RunHit const& hit : hits)
                list.push_back({{"enemy", hit.enemy}, {"member", hit.member}, {"time_s", Seconds(hit.startMs)},
                    {"seconds", Seconds(hit.endMs - hit.startMs)}});
            return list;
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
        for (RunPull const& pull : r.pulls)
            pulls.push_back({{"pack", pull.pack}, {"time_s", Seconds(pull.ms)}});
        j["pulls"] = pulls;
        j["threat_losses"] = Hits(r.threatLosses);
        j["lost_mobs"] = Hits(r.lostMobs);

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
}
