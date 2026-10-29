/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#ifndef WOWAI_GAME_FOREVER_TALENTS_H
#define WOWAI_GAME_FOREVER_TALENTS_H

// The talent helpers of the Forever server code (spec jvbastel/WOWAI#204): which rank of a talent a unit
// has, and the value of that rank. Internal to src/game/Forever; the stock code includes Forever.h. The
// rank arrays are in ForeverSpellIds.h (rank 1 first); the pure part (Contains, KnownRank) is there too.

#include "Common.h"
#include "ForeverSpellIds.h"
#include "SpellDefines.h"

#include <cstddef>

class SpellEntry;
class Unit;

namespace Forever
{
namespace Talents
{
    // The sum of the DUMMY aura values (effect amount) of the rank spells `ranks` on `unit`: the talent
    // value of the rank that it has, for a talent whose ranks are passive dummy auras.
    int32 SumDummy(Unit const* unit, uint32 const* ranks, std::size_t count);

    // The highest rank spell of `ranks` whose aura `unit` has, or nullptr.
    SpellEntry const* RankByAura(Unit const* unit, uint32 const* ranks, std::size_t count);

    // The highest rank spell of `ranks` that `unit` knows (Unit::HasSpell), or nullptr. For a talent whose
    // aura can be missing (a weapon requirement).
    SpellEntry const* RankBySpell(Unit const* unit, uint32 const* ranks, std::size_t count);

    // The value of effect `eff` of a rank spell (the spec value: base points + 1), or 0 with no rank.
    int32 RankEffectValue(SpellEntry const* rank, SpellEffectIndex eff);

    template <std::size_t N>
    int32 SumDummy(Unit const* unit, uint32 const (&ranks)[N]) { return SumDummy(unit, ranks, N); }

    template <std::size_t N>
    SpellEntry const* RankByAura(Unit const* unit, uint32 const (&ranks)[N]) { return RankByAura(unit, ranks, N); }

    template <std::size_t N>
    SpellEntry const* RankBySpell(Unit const* unit, uint32 const (&ranks)[N]) { return RankBySpell(unit, ranks, N); }
}
}

#endif
