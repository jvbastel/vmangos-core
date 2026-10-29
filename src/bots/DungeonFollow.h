#ifndef WOWAI_BOTS_DUNGEON_FOLLOW_H
#define WOWAI_BOTS_DUNGEON_FOLLOW_H

#include "BotTypes.h"
#include "CommandState.h"
#include "CoordinatorChoice.h"
#include "GroupTank.h"

#include <cstdint>
#include <vector>

// jvbastel/WOWAI#196 (spec #188, stories 42 and 43): when the player tank
// goes into an instance or out of one, the game layer teleports the bots of
// the group after them, next to them, with no command. Any dungeon.
namespace Bots
{
    // A failed teleport (the game refused it) is tried again after this time.
    std::int64_t const DungeonFollowRetryMs = 5000;

    // Where a member of the group is, as the game layer knows it.
    struct TravelFacts
    {
        // In the world, so not in a far teleport (the loading screen).
        bool inWorld;
        // Alive: not dead and not a ghost.
        bool alive;
        MapPlace place;
        // The map of `place` is a dungeon (an instance).
        bool inDungeon;
    };

    struct DungeonFollowFacts
    {
        // The group plays a scripted run: the run teleports it itself.
        bool scriptedRun;
        // The group is a player-tank group: its tank is a human
        // (GroupTankOf, GroupTank.h).
        bool playerTank;
        TravelFacts tank;
        TravelFacts bot;
        // The command state of the bot now (CommandStateNow).
        CommandState command;
        // The time since the last teleport of this bot after the tank.
        std::int64_t msSinceLastTry;
    };

    // The bot is teleported to the tank now: the group is a player-tank group
    // and no scripted run, both are alive and in the world, they are in two
    // places, and one of the two places is a dungeon. A dead bot or a ghost
    // is left to today's death handling. A bot told to stay stays.
    bool FollowsTankThroughInstance(DungeonFollowFacts const& facts);
}

#endif
