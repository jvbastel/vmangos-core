#ifndef WOWAI_BOTS_BOT_TYPES_H
#define WOWAI_BOTS_BOT_TYPES_H

#include <algorithm>
#include <cstdint>

// Plain types that the decision library shares with the game layer. The
// library has no game types: it refers to units by GUID, never by pointer.
namespace Bots
{
    // True when `values` holds `value`.
    template <typename Values, typename Value>
    bool Contains(Values const& values, Value const& value)
    {
        return std::find(values.begin(), values.end(), value) != values.end();
    }

    // The full 64-bit GUID of a unit, as the game layer has it in ObjectGuid.
    using Guid = std::uint64_t;
    // No unit, for example no target.
    Guid const NoGuid = 0;

    using SpellId = std::uint32_t;
    SpellId const NoSpell = 0;

    // The kind of spell that a unit casts, for the interrupts.
    enum class CastKind
    {
        None,  // The unit casts nothing.
        Heal,  // A heal.
        Harm,  // A harmful spell, for example a damage spell.
        Other, // For example a buff.
    };

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
