#ifndef WOWAI_BOTS_BOT_VIEW_H
#define WOWAI_BOTS_BOT_VIEW_H

#include "BotTypes.h"
#include "Ranged.h"
#include "RoleChoice.h"

#include <limits>
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

    // The spell that a unit casts now.
    struct CastInfo
    {
        SpellId spell; // NoSpell when the unit casts nothing.
        CastKind kind;
        // Kick or Shield Bash can stop the cast now.
        bool interruptible;
    };

    // jvbastel/WOWAI#134: the amounts of one rank of any spell, cast by the
    // bot (BotView::GetRankAmounts). The rank choice of a heal reads them
    // (SpellBook.h, CastableHealRank).
    struct RankAmounts
    {
        // The average direct heal of the rank, from the spell data: no
        // +healing, no crit, no talent. 0 for a spell without a direct heal
        // (Renew, a damage spell).
        std::uint32_t averageHeal;
        // The mana cost of the rank. 0 for a spell that costs no mana.
        std::uint32_t manaCost;
    };

    // The kinds of debuff that a dispel spell removes.
    enum class DispelKind
    {
        Magic,
        Curse,
        Disease,
        Poison,
    };

    // A living hostile creature near the bot, which a walk could pull
    // (SafePlace.h).
    struct NearEnemy
    {
        Position position;
        float aggroYards; // Its aggro distance for this bot, from its center.
        Guid guid = NoGuid;
    };

    // The threat ratio of a unit that alone has threat on an enemy
    // (BotView::GetThreatRatio): no other member rivals it.
    float const UnrivalledThreat = std::numeric_limits<float>::max();

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
        // Rage in rage points (0-100), as the player sees it.
        virtual Power GetPower(Guid unit) const = 0;
        // The kind of power of unit `unit`: mana, rage, or energy. A warrior
        // and a druid in bear form have rage. None for a unit that the bot
        // cannot see.
        virtual PowerKind GetPowerKind(Guid unit) const = 0;
        // True when unit `unit` is a player of a class that wears cloth: a
        // mage, a warlock or a priest (jvbastel/WOWAI#168, the order in
        // which the tank takes loose enemies). False for a unit that the
        // bot cannot see.
        virtual bool WearsCloth(Guid unit) const = 0;
        // True when unit `unit` cannot move: it is rooted or stunned
        // (jvbastel/WOWAI#169: the tank goes to such a member to take its
        // attacker). False for a unit that the bot cannot see.
        virtual bool IsRootedOrStunned(Guid unit) const = 0;
        // The distance in yards from the bot to unit `unit`. A large number
        // for a unit that the bot cannot see.
        virtual float GetDistance(Guid unit) const = 0;
        // The distance in yards from the bot to unit `unit` as spell ranges
        // measure it: between the combat reaches, not the bodies
        // (Spell::CheckRange). A large number for a unit that the bot
        // cannot see.
        virtual float GetSpellDistance(Guid unit) const = 0;
        // The same, with the bot at `place` (on the ground there): for a
        // cast from a spot that the bot has not reached yet (Ranged.h).
        virtual float GetSpellDistanceFrom(Position const& place, Guid unit) const = 0;
        // The distance in yards between units `a` and `b`. A large number
        // when the bot cannot see one of them.
        virtual float GetDistanceBetween(Guid a, Guid b) const = 0;
        // True when unit `unit` is in combat.
        virtual bool IsInCombat(Guid unit) const = 0;
        // The unit that unit `unit` attacks, or NoGuid.
        virtual Guid GetVictim(Guid unit) const = 0;
        // The units that attack unit `unit`.
        virtual std::vector<Guid> GetAttackers(Guid unit) const = 0;
        // The threat of unit `unit` on enemy `enemy`, as a ratio of the
        // highest threat of any other member of the bot's group (or a pet of
        // one) on that enemy, as a threat meter shows it. 1.5: a lead of
        // 150%. 0 when `unit` has no threat on it, or the bot cannot see
        // either unit. UnrivalledThreat when no other member has threat on it.
        virtual float GetThreatRatio(Guid unit, Guid enemy) const = 0;
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
        // True when the level of unit `target` allows rank `rank` of a spell.
        // A buff rank may be at most 10 levels above the target, else the
        // cast fails with SPELL_FAILED_LOWLEVEL. Each other spell is allowed.
        virtual bool LevelAllowsRank(SpellId rank, Guid target) const = 0;
        // True when a state of the bot keeps it from casting spell `spell`
        // now: a stun, a fear, a confuse; a silence or a school lockout for
        // a magic spell; a pacify for a physical one (Spell::CheckCasterAuras).
        virtual bool IsCastLocked(SpellId spell) const = 0;
        // jvbastel/WOWAI#134: the average direct heal and the mana cost of
        // rank `rank` of a spell, cast by the bot. Any spell: one with no
        // direct heal (Renew) has averageHeal 0. {0, 0} for an unknown spell.
        virtual RankAmounts GetRankAmounts(SpellId rank) const = 0;

        // The members of the bot's group that the bot can see, the bot
        // first. Only the bot when it is in no group.
        virtual std::vector<Guid> GetGroupMembers() const = 0;

        // Places, for the boss reactions (BossReactions.h).
        // The creature entry of unit `unit`. 0 for a player and for a unit
        // that the bot cannot see.
        virtual std::uint32_t GetEntry(Guid unit) const = 0;
        // True for a creature in the boss list of a route file of the bot's
        // map (Route.h, BossList).
        virtual bool IsListedBoss(Guid unit) const = 0;
        // True when the bot is the loot recipient of creature `unit` (it
        // holds the tap), alive or dead.
        virtual bool HoldsTap(Guid unit) const = 0;
        // The position of unit `unit`. {0, 0, 0} for a unit that the bot
        // cannot see.
        virtual Position GetPosition(Guid unit) const = 0;
        // The distance in yards from `from` to the first wall in direction
        // `angle` (radians, as the orientation of a unit: 0 is +x, pi/2 is
        // +y), at the height of a body. `maxYards` when no wall is nearer.
        virtual float GetWallDistance(Position const& from, float angle, float maxYards) const = 0;
        // The ground at `place` (x and y; z is a height near it) lies in
        // harmful liquid, such as lava or slime. The one lava check of the
        // bots' safe places (SafePlace.h): the liquid of the map grid, from
        // which the game deals the lava damage. The lava polygons of the
        // navmesh only steer FindGroundPath round the lava.
        virtual bool IsHarmfulGround(Position const& place) const = 0;
        // A bot on the ground at `place` would see unit `unit` (line of
        // sight, as for a cast). False for a unit that the bot cannot see.
        virtual bool CanSeeFrom(Position const& place, Guid unit) const = 0;
        // A bot on the ground at `from` would see a bot on the ground at
        // `to` (line of sight, as for a cast).
        virtual bool CanSeeBetween(Position const& from, Position const& to) const = 0;

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
        // True while a scripted move of the test driver walks: the bot is
        // the test driver of a scripted run, and its `move` has not arrived
        // or stopped yet. Then the script decides, and the bot does not rest.
        virtual bool IsScriptedMoveInProgress() const = 0;
        // Places to move to, for the safe places of SafePlace.h.
        // `place` on the ground near its height z, as MoveTo puts it.
        virtual Position GetGround(Position const& place) const = 0;
        // A walk on the ground from `from` to `to`, as corners, from the
        // navmesh. The navmesh leaves out lava and slime polygons, so the
        // path goes round them where it can. False when `to` is off the
        // navmesh or has no path, and with no navmesh at all.
        virtual bool FindGroundPath(Position const& from, Position const& to, std::vector<Position>& path) const = 0;
        // Every living hostile creature within `yards` of the bot, seen or
        // not, but no pet.
        virtual std::vector<NearEnemy> GetEnemiesNear(float yards) const = 0;

        // The ranged pull of the tank (Ranged.h, WarriorModule.h).
        // The ranged weapon that the bot wears, and its ammo.
        virtual RangedWeapon GetRangedWeapon() const = 0;
        // jvbastel/WOWAI#133: the target of the shot of the bot that is
        // going, or NoGuid for none. A shot is any auto-repeat spell (a
        // wand's Shoot, a bow's Shoot Bow, and so on): it repeats until a
        // cast or BotActions::StopShot stops it.
        virtual Guid GetShotTarget() const = 0;
        // The level of unit `unit`. 0 for a unit that the bot cannot see.
        virtual std::uint32_t GetLevel(Guid unit) const = 0;
        // True when unit `unit` moves now, for example a pulled enemy on its
        // way to the tank.
        virtual bool IsMoving(Guid unit) const = 0;
        // True when the bot swings at its victim in melee. The attack of
        // BotActions::ChaseToRange has no melee swing; BotActions::Attack
        // starts it.
        virtual bool IsMeleeAttacking() const = 0;
        // True when BotActions::TopUpAmmo(topUp) has room: in the bags for
        // the missing arrows or bullets, or in the worn thrown stack.
        virtual bool HasRoomFor(AmmoTopUp const& topUp) const = 0;

        // jvbastel/WOWAI#189: the tank's target. The unit that unit `unit`
        // targets now: the selection of a player (the target frame of a
        // player tank), the target of a creature. NoGuid for none, and for a
        // unit that the bot cannot see. Friend or enemy: the caller decides.
        virtual Guid GetTargetOf(Guid unit) const = 0;

        // jvbastel/WOWAI#191: the group's marks. The enemy that each mark of
        // the bot's group is on: the skull and the cross, NoGuid for a mark
        // that is not set, and both NoGuid without a group. The player sets
        // them; bots only read them (no bot action sets a mark).
        struct GroupMarks
        {
            Guid skull = NoGuid;
            Guid cross = NoGuid;
        };
        virtual GroupMarks GetGroupMarks() const = 0;
        // jvbastel/WOWAI#197: the role choice of group member `member`, from
        // the role choice add-on (RoleChoice.h). RoleChoice::None for a bot,
        // and for a human with no add-on or no pick.
        virtual RoleChoice GetRoleChoice(Guid member) const = 0;
    };
}

#endif
