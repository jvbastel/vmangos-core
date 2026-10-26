#ifndef WOWAI_BOTS_THINK_TIMER_H
#define WOWAI_BOTS_THINK_TIMER_H

#include <cstdint>

// When a bot thinks. A bot empties its inbox at each update, but it thinks
// (decides what to do) only when its think timer is due. The interval
// depends on the state of the bot: often in combat, rarely far from players.
namespace Bots
{
    enum class ThinkState
    {
        Combat,     // In combat.
        NearPlayer, // Out of combat, near a player.
        Far,        // Out of combat, far from any player.
    };

    // The think intervals, from the settings `Bots.ThinkMs.Combat`,
    // `Bots.ThinkMs.NearPlayer`, and `Bots.ThinkMs.Far`.
    struct ThinkSettings
    {
        std::uint32_t combatMs = 100;
        std::uint32_t nearPlayerMs = 500;
        std::uint32_t farMs = 3000;
    };

    ThinkState ThinkStateFor(bool inCombat, bool nearPlayer);

    std::uint32_t ThinkInterval(ThinkState state, ThinkSettings const& settings);

    class ThinkTimer
    {
    public:
        explicit ThinkTimer(ThinkSettings settings = ThinkSettings());

        // Adds the time since the last update. True when the bot must think
        // now: at the first update, and then when the time since the last
        // think reaches the interval of the current state. So a bot that
        // enters combat during a long wait thinks at once.
        bool Advance(std::uint32_t diffMs, ThinkState state);

    private:
        ThinkSettings m_settings;
        std::uint32_t m_sinceThinkMs;
        bool m_thoughtOnce;
    };
}

#endif
