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
    // How the bot runs after a unit in a fight.
    struct ChaseState
    {
        Guid target; // NoGuid when the bot chases no unit.
        bool behind; // The bot runs behind the unit (BotActions::ChaseBehind).
        // A chase from BotActions::ChaseToRange is not behind.
    };

    // The kind of spell that a unit casts, for the interrupts.
    enum class CastKind
    {
        None,  // The unit casts nothing.
        Heal,  // A heal.
        Harm,  // A harmful spell, for example a damage spell.
        Other, // For example a buff.
    };

    // The spell that a unit casts now.
    struct CastInfo
    {
        SpellId spell; // NoSpell when the unit casts nothing.
        CastKind kind;
        // Kick or Shield Bash can stop the cast now.
        bool interruptible;
    };

    // The kinds of debuff that a dispel spell removes.
    enum class DispelKind
    {
        Magic,
        Curse,
        Disease,
        Poison,
    };

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

        // The spell that unit `unit` casts now, with its kind.
        virtual CastInfo GetCast(Guid unit) const = 0;

        // The power of unit `unit`. {0, 0} for a unit that the bot cannot see.
        virtual Power GetPower(Guid unit) const = 0;
        // The distance in yards from the bot to unit `unit`. A large number
        // for a unit that the bot cannot see.
        virtual float GetDistance(Guid unit) const = 0;
        // The distance in yards between units `a` and `b`. A large number
        // when the bot cannot see one of them.
        virtual float GetDistanceBetween(Guid a, Guid b) const = 0;
        // True when unit `unit` is in combat.
        virtual bool IsInCombat(Guid unit) const = 0;
        // The unit that unit `unit` attacks, or NoGuid.
        virtual Guid GetVictim(Guid unit) const = 0;
        // The units that attack unit `unit`.
        virtual std::vector<Guid> GetAttackers(Guid unit) const = 0;
        // True when unit `unit` has an aura of any rank of the spell whose
        // first rank is `firstRank`.
        virtual bool HasAura(Guid unit, SpellId firstRank) const = 0;
        // True when unit `unit` has a harmful aura that a dispel of kind
        // `kind` removes.
        virtual bool HasDebuff(Guid unit, DispelKind kind) const = 0;
        // The unit that the bot follows now, or NoGuid.
        virtual Guid GetFollowTarget() const = 0;
        // The unit that the bot chases now, and how.
        virtual ChaseState GetChase() const = 0;
        // The pet of the bot, for example the Imp of a Warlock, or NoGuid.
        virtual Guid GetPet() const = 0;
        // The combo points of the bot on its current target.
        virtual std::uint32_t GetComboPoints() const = 0;
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

        // Places, for the boss reactions (BossReactions.h).
        // The creature entry of unit `unit`. 0 for a player and for a unit
        // that the bot cannot see.
        virtual std::uint32_t GetEntry(Guid unit) const = 0;
        // The position of unit `unit`. {0, 0, 0} for a unit that the bot
        // cannot see.
        virtual Position GetPosition(Guid unit) const = 0;
        // The distance in yards from `from` to the first wall in direction
        // `angle` (radians, as the orientation of a unit: 0 is +x, pi/2 is
        // +y), at the height of a body. `maxYards` when no wall is nearer.
        virtual float GetWallDistance(Position const& from, float angle, float maxYards) const = 0;
        // The ground at `place` (x and y; z is a height near it) lies in
        // harmful liquid, such as lava or slime.
        virtual bool IsHarmfulGround(Position const& place) const = 0;
        // A bot on the ground at `place` would see unit `unit` (line of
        // sight, as for a cast). False for a unit that the bot cannot see.
        virtual bool CanSeeFrom(Position const& place, Guid unit) const = 0;

        // Death and rest.
        // The life state of unit `unit`. Alive for a unit that the bot
        // cannot see. The leader of the bot's group is found on any map.
        virtual LifeState GetLifeState(Guid unit) const = 0;
        // The leader of the bot's group, or NoGuid without a group.
        virtual Guid GetGroupLeader() const = 0;
        // True when unit `unit` is alive and knows a spell that resurrects a
        // dead player.
        virtual bool CanResurrect(Guid unit) const = 0;
        // The mana of unit `unit`. {0, 0} for a unit without mana.
        virtual Power GetMana(Guid unit) const = 0;
        // True when the bot eats now: it has the aura of its food.
        virtual bool IsEating() const = 0;
        // True when the bot drinks now: it has the aura of its drink.
        virtual bool IsDrinking() const = 0;
        // A place within `yards` of unit `unit`, in its line of sight, for a
        // spell on it, for example the resurrect of a corpse. The bot walks
        // there on the ground on a path that pulls no enemy (the path and
        // the place keep out of the aggro distance of every living enemy)
        // and crosses no harmful ground such as lava (IsHarmfulGround), and
        // the unit is in sight from the place (CanSeeFrom). The place nearest to
        // the bot. False when there is none, and for a unit too far away to
        // know the enemies on the way (about 80 yards).
        virtual bool FindSafeCastPlace(Guid unit, float yards, Position& place) const = 0;
    };
}

#endif
