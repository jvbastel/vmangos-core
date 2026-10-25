#include "RouteQuests.h"

#include <gtest/gtest.h>

namespace
{
    Bots::Route QuestRoute()
    {
        Bots::RouteResult result = Bots::ParseRoute(
            "route rfc 389\n"
            "quest 5722\n"
            "visit q5722_corpse\n"
            "quest 5723\n"
            "kill 11318 2\n"
            "quest 5725\n"
            "item 14395 1 11322\n"
            "point p1 0 0 0 2\n"
            "point q5722_corpse 1 1 1 2 quest 5722\n"
            "pack pack1 2 2 2\n"
            "mob 1 11318\n"
            "point pull_pack2 3 3 3 2 quest 5723\n"
            "pack pack2 4 4 4 quest 5723\n"
            "mob 2 11318\n"
            "point p2 5 5 5 2\n");
        EXPECT_TRUE(result.ok) << result.error;
        return result.route;
    }

    std::vector<std::string> StepNames(Bots::Route const& route, std::vector<Bots::RouteStep> const& steps)
    {
        std::vector<std::string> names;
        for (Bots::RouteStep const& step : steps)
            names.push_back(step.kind == Bots::RouteStepKind::Point ? route.points[step.index].name
                                                                    : route.packs[step.index].name);
        return names;
    }
}

TEST(RouteRunOption, AFullRunDoesTheQuestsByDefault)
{
    Bots::Route const route = QuestRoute();

    EXPECT_EQ(StepNames(route, Bots::RunSteps(route, Bots::RunQuests::Do)),
        (std::vector<std::string>{"p1", "q5722_corpse", "pack1", "pull_pack2", "pack2", "p2"}));
}

TEST(RouteRunOption, ARunWithoutQuestsSkipsTheQuestPointsAndPacks)
{
    Bots::Route const route = QuestRoute();

    EXPECT_EQ(StepNames(route, Bots::RunSteps(route, Bots::RunQuests::Skip)),
        (std::vector<std::string>{"p1", "pack1", "p2"}));
}

TEST(QuestProgress, CountsKillsLootAndVisitsForTheResultFile)
{
    Bots::QuestProgress progress(QuestRoute());
    EXPECT_TRUE(progress.CompletedQuests().empty());

    progress.OnKill(11318);
    progress.OnKill(11319);
    progress.OnVisit("q5722_corpse");
    progress.OnVisit("p1");

    std::vector<Bots::ObjectiveProgress> const& objectives = progress.Objectives();
    ASSERT_EQ(objectives.size(), 3u);
    EXPECT_EQ(objectives[0].quest, 5722u);
    EXPECT_EQ(objectives[0].done, 1u);
    EXPECT_TRUE(objectives[0].Complete());
    EXPECT_EQ(objectives[1].done, 1u);
    EXPECT_FALSE(objectives[1].Complete());
    EXPECT_EQ(progress.CompletedQuests(), (std::vector<std::uint32_t>{5722}));

    progress.OnKill(11318);
    progress.OnKill(11318); // More than the count stays at the count.
    progress.OnLoot(14395, 1);
    EXPECT_EQ(progress.Objectives()[1].done, 2u);
    EXPECT_EQ(progress.Objectives()[2].done, 1u);
    EXPECT_EQ(progress.CompletedQuests(), (std::vector<std::uint32_t>{5722, 5723, 5725}));
}
