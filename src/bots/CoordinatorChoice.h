#ifndef WOWAI_BOTS_COORDINATOR_CHOICE_H
#define WOWAI_BOTS_COORDINATOR_CHOICE_H

#include <cstdint>

namespace Bots
{
    // A map and its instance. Two instances of one dungeon are two places.
    struct MapPlace
    {
        std::uint32_t map;
        std::uint32_t instance;
    };

    // Where the leader of the group is, when the bot can know it.
    struct LeaderPlace
    {
        bool known;
        MapPlace place;

        static LeaderPlace On(MapPlace place) { return {true, place}; }
        // The leader is offline or not found.
        static LeaderPlace Unknown() { return {false, {0, 0}}; }
    };

    enum class CoordinatorChoice
    {
        Group, // The coordinator of the bot's group.
        Place, // The coordinator of the group's bots in the bot's place, on
               // another map or instance than the leader.
        Own,   // The bot's own coordinator: the bot has no group.
    };

    CoordinatorChoice ChooseCoordinator(bool inGroup, MapPlace bot, LeaderPlace leader);
}

#endif
