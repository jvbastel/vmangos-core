#ifndef WOWAI_BOTS_ROUTE_QUESTS_H
#define WOWAI_BOTS_ROUTE_QUESTS_H

#include "Route.h"

#include <cstdint>
#include <string>
#include <vector>

// The quests of a route in a run: the run option that skips them, and the
// progress of each objective for the result file of the run. Only the
// player and the test driver have quests; bots get no quest log entries.
namespace Bots
{
    enum class RunQuests
    {
        Do,   // The default of a full run.
        Skip, // Leaves out the points and packs that are only there for a quest.
    };

    // The steps of the route that a run walks, in route order.
    std::vector<RouteStep> RunSteps(Route const& route, RunQuests quests);

    struct ObjectiveProgress
    {
        std::uint32_t quest;
        QuestObjective objective;
        std::uint32_t done; // At most the count of the objective, 1 for a visit.

        std::uint32_t Needed() const;
        bool Complete() const { return done >= Needed(); }
    };

    // The objectives of the route's quests and how much of each is done.
    class QuestProgress
    {
    public:
        explicit QuestProgress(Route const& route);

        void OnKill(std::uint32_t entry);
        void OnLoot(std::uint32_t item, std::uint32_t count);
        void OnVisit(std::string const& point);

        // In the order of the quest list of the route.
        std::vector<ObjectiveProgress> const& Objectives() const { return m_objectives; }
        // The quests with every objective complete, in quest list order.
        std::vector<std::uint32_t> CompletedQuests() const;

    private:
        void Add(ObjectiveProgress& objective, std::uint32_t amount);

        std::vector<ObjectiveProgress> m_objectives;
    };
}

#endif
