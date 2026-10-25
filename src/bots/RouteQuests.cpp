#include "RouteQuests.h"

#include <algorithm>

namespace Bots
{
    std::vector<RouteStep> RunSteps(Route const& route, RunQuests quests)
    {
        std::vector<RouteStep> steps;
        for (RouteStep const& step : route.steps)
        {
            std::uint32_t const quest = step.kind == RouteStepKind::Point ? route.points[step.index].quest
                                                                          : route.packs[step.index].quest;
            if (quests == RunQuests::Do || quest == 0)
                steps.push_back(step);
        }
        return steps;
    }

    std::uint32_t ObjectiveProgress::Needed() const
    {
        return objective.kind == QuestObjectiveKind::Visit ? 1 : objective.count;
    }

    QuestProgress::QuestProgress(Route const& route)
    {
        for (RouteQuest const& quest : route.quests)
            for (QuestObjective const& objective : quest.objectives)
                m_objectives.push_back({quest.id, objective, 0});
    }

    void QuestProgress::Add(ObjectiveProgress& objective, std::uint32_t amount)
    {
        objective.done = std::min(objective.Needed(), objective.done + amount);
    }

    void QuestProgress::OnKill(std::uint32_t entry)
    {
        for (ObjectiveProgress& progress : m_objectives)
            if (progress.objective.kind == QuestObjectiveKind::Kill && progress.objective.entry == entry)
                Add(progress, 1);
    }

    void QuestProgress::OnLoot(std::uint32_t item, std::uint32_t count)
    {
        for (ObjectiveProgress& progress : m_objectives)
            if (progress.objective.kind == QuestObjectiveKind::Item && progress.objective.item == item)
                Add(progress, count);
    }

    void QuestProgress::OnVisit(std::string const& point)
    {
        for (ObjectiveProgress& progress : m_objectives)
            if (progress.objective.kind == QuestObjectiveKind::Visit && progress.objective.point == point)
                Add(progress, 1);
    }

    std::vector<std::uint32_t> QuestProgress::CompletedQuests() const
    {
        std::vector<std::uint32_t> quests;
        for (ObjectiveProgress const& progress : m_objectives)
            if (std::find(quests.begin(), quests.end(), progress.quest) == quests.end())
                quests.push_back(progress.quest);
        std::vector<std::uint32_t> complete;
        for (std::uint32_t quest : quests)
        {
            bool all = true;
            for (ObjectiveProgress const& progress : m_objectives)
                if (progress.quest == quest)
                    all = all && progress.Complete();
            if (all)
                complete.push_back(quest);
        }
        return complete;
    }
}
