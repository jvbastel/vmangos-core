#ifndef WOWAI_BOTS_BOT_VIEW_H
#define WOWAI_BOTS_BOT_VIEW_H

#include "BotTypes.h"

#include <vector>

namespace Bots
{
    // The queries of one bot about the game. With BotActions, the only way
    // from the decision library to the game (ADR 0002 of WOWAI). The real
    // adapter in src/game/Bots wraps Player and Unit, and the fake adapter in
    // tests/ serves the tests. Each new query that a trigger needs is a new
    // call here, and both adapters implement it.
    class BotView
    {
    public:
        virtual ~BotView() = default;

        // The GUID of the bot itself.
        virtual Guid GetSelf() const = 0;
        // The health of unit `unit`. {0, 0} for a unit that the bot cannot see.
        virtual Health GetHealth(Guid unit) const = 0;
        // The spell that unit `unit` casts now, or NoSpell.
        virtual SpellId GetCurrentCast(Guid unit) const = 0;

        // The power of unit `unit`. {0, 0} for a unit that the bot cannot see.
        virtual Power GetPower(Guid unit) const = 0;
        // The distance in yards from the bot to unit `unit`. A large number
        // for a unit that the bot cannot see.
        virtual float GetDistance(Guid unit) const = 0;
        // True when unit `unit` is in combat.
        virtual bool IsInCombat(Guid unit) const = 0;
        // The unit that unit `unit` attacks, or NoGuid.
        virtual Guid GetVictim(Guid unit) const = 0;
        // The units that attack unit `unit`.
        virtual std::vector<Guid> GetAttackers(Guid unit) const = 0;
        // True when unit `unit` has an aura of any rank of the spell whose
        // first rank is `firstRank`.
        virtual bool HasAura(Guid unit, SpellId firstRank) const = 0;
        // The unit that the bot follows now, or NoGuid.
        virtual Guid GetFollowTarget() const = 0;
        // True when the bot has corpse loot to do now: it is busy with a
        // corpse, or a corpse that it may loot is near. `mayMove`: the bot
        // may walk to a corpse.
        virtual bool HasLootWork(bool mayMove) const = 0;

        // Spells and ranks, for the spell book helper (SpellBook.h).
        // True when the bot knows spell `spell`.
        virtual bool KnowsSpell(SpellId spell) const = 0;
        // The next rank of spell `spell` in its chain of ranks, or NoSpell
        // for the highest rank and for a spell without ranks.
        virtual SpellId GetNextRank(SpellId spell) const = 0;
        // True when the bot can cast spell `spell` (a known rank) on unit
        // `target` now: the spell is ready, the global cooldown is over, and
        // the power, the stance, the aura states, and the range allow it.
        virtual bool CanCast(SpellId spell, Guid target) const = 0;

        // The members of the bot's group that the bot can see, the bot
        // first. Only the bot when it is in no group.
        virtual std::vector<Guid> GetGroupMembers() const = 0;

        // Death and rest.
        // The life state of unit `unit`. Alive for a unit that the bot
        // cannot see. The leader of the bot's group is found on any map.
        virtual LifeState GetLifeState(Guid unit) const = 0;
        // The leader of the bot's group, or NoGuid without a group.
        virtual Guid GetGroupLeader() const = 0;
        // The distance in yards from unit `unit` to the corpse of the bot. A
        // large number when the bot has no corpse, or when the unit is on
        // another map than the corpse. The leader is found on any map.
        virtual float GetCorpseDistance(Guid unit) const = 0;
        // True when unit `unit` is alive and knows a spell that resurrects a
        // dead player.
        virtual bool CanResurrect(Guid unit) const = 0;
        // The mana of unit `unit`. {0, 0} for a unit without mana.
        virtual Power GetMana(Guid unit) const = 0;
        // True when the bot eats now: it has the aura of its food.
        virtual bool IsEating() const = 0;
        // True when the bot drinks now: it has the aura of its drink.
        virtual bool IsDrinking() const = 0;
    };
}

#endif
