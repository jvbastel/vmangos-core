#ifndef WOWAI_BOTS_BOT_TYPES_H
#define WOWAI_BOTS_BOT_TYPES_H

#include <cstdint>

// Plain types that the decision library shares with the game layer. The
// library has no game types: it refers to units by GUID, never by pointer.
namespace Bots
{
    // The full 64-bit GUID of a unit, as the game layer has it in ObjectGuid.
    using Guid = std::uint64_t;
    // No unit, for example no target.
    Guid const NoGuid = 0;

    using SpellId = std::uint32_t;
    SpellId const NoSpell = 0;

    struct Position
    {
        float x;
        float y;
        float z;
    };

    struct Health
    {
        std::uint32_t current;
        std::uint32_t max;
    };

    // Alive, dead, or released: a ghost.
    enum class LifeState
    {
        Alive,
        Dead,  // A dead body that has not released.
        Ghost, // Released: the spirit, with a corpse where it died.
    };

    // The power of a unit in its own kind: mana, rage, or energy.
    struct Power
    {
        std::uint32_t current;
        std::uint32_t max;
    };
}

#endif
