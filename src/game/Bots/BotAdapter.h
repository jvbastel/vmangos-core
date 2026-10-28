/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#ifndef WOWAI_GAME_BOTADAPTER_H
#define WOWAI_GAME_BOTADAPTER_H

#include "BotActions.h"
#include "BotView.h"
#include "ThreatWatch.h"

#include "Common.h"

class BotLoot;
class Player;
class Unit;
class SpellEntry;

// The real adapter of the decision library on one bot: BotView answers from
// the Player and the units of its map, and BotActions acts as the Player.
// Units are GUIDs in the library; the adapter finds them on the map of the
// bot, so a unit on another map is a unit that the bot cannot see.
//
// Threads: only on the map thread of the bot, in BotAI::UpdateAI.
class BotAdapter : public Bots::BotView, public Bots::BotActions
{
    public:
        // `loot`: the corpse loot of the bot, for HasLootWork and Loot. Null:
        // the bot has no loot work.
        explicit BotAdapter(Player* me, BotLoot* loot = nullptr) : me(me), m_loot(loot) {}

        // What any member does to enemies now, a bot or the test driver, for
        // the lost-mob watch of the scripted runs (Bots::MemberAttacks has
        // the rule). On the world thread, in BotRun.
        static Bots::MemberAttacks AttacksOf(Player const* member, bool bot);
        // The spell that a unit casts now, as GetCast sees it. Also on the
        // world thread, in BotRun (the interrupts of a scripted run).
        static Bots::CastInfo CastOf(Unit const* unit);
        // The kind of spell `spell` for the interrupts: Other without a spell.
        static Bots::CastKind CastKindOf(SpellEntry const* spell);
        // The unit that a player runs after (a chase), or null. Also on the
        // world thread, in BotRun (the chases of a scripted run).
        static Unit* ChaseTargetOf(Player const* player);

        // BotView
        Bots::Guid GetSelf() const override;
        Bots::Health GetHealth(Bots::Guid unit) const override;
        Bots::SpellId GetCurrentCast(Bots::Guid unit) const override;
        Bots::CastInfo GetCast(Bots::Guid unit) const override;
        Bots::Power GetPower(Bots::Guid unit) const override;
        Bots::PowerKind GetPowerKind(Bots::Guid unit) const override;
        bool WearsCloth(Bots::Guid unit) const override;
        bool IsRootedOrStunned(Bots::Guid unit) const override;
        float GetDistance(Bots::Guid unit) const override;
        float GetSpellDistance(Bots::Guid unit) const override;
        float GetSpellDistanceFrom(Bots::Position const& place, Bots::Guid unit) const override;
        float GetDistanceBetween(Bots::Guid a, Bots::Guid b) const override;
        bool IsInCombat(Bots::Guid unit) const override;
        Bots::Guid GetVictim(Bots::Guid unit) const override;
        std::vector<Bots::Guid> GetAttackers(Bots::Guid unit) const override;
        float GetThreatRatio(Bots::Guid unit, Bots::Guid enemy) const override;
        bool HasAura(Bots::Guid unit, Bots::SpellId firstRank) const override;
        bool HasDebuff(Bots::Guid unit, Bots::DispelKind kind) const override;
        Bots::Guid GetFollowTarget() const override;
        Bots::ChaseState GetChase() const override;
        uint32 GetComboPoints() const override;
        Bots::Guid GetPet() const override;
        bool HasLootWork(bool mayMove) const override;
        bool KnowsSpell(Bots::SpellId spell) const override;
        Bots::SpellId GetNextRank(Bots::SpellId spell) const override;
        bool CanCast(Bots::SpellId spell, Bots::Guid target) const override;
        bool LevelAllowsRank(Bots::SpellId rank, Bots::Guid target) const override;
        bool IsCastLocked(Bots::SpellId spell) const override;
        Bots::RankAmounts GetRankAmounts(Bots::SpellId rank) const override;
        std::vector<Bots::Guid> GetGroupMembers() const override;
        uint32 GetEntry(Bots::Guid unit) const override;
        bool IsListedBoss(Bots::Guid unit) const override;
        bool HoldsTap(Bots::Guid unit) const override;
        Bots::Position GetPosition(Bots::Guid unit) const override;
        float GetWallDistance(Bots::Position const& from, float angle, float maxYards) const override;
        bool IsHarmfulGround(Bots::Position const& place) const override;
        bool CanSeeFrom(Bots::Position const& place, Bots::Guid unit) const override;
        Bots::LifeState GetLifeState(Bots::Guid unit) const override;
        Bots::Guid GetGroupLeader() const override;
        bool CanResurrect(Bots::Guid unit) const override;
        Bots::Power GetMana(Bots::Guid unit) const override;
        bool IsEating() const override;
        bool IsDrinking() const override;
        bool IsScriptedMoveInProgress() const override;
        Bots::Position GetGround(Bots::Position const& place) const override;
        bool CanSeeBetween(Bots::Position const& from, Bots::Position const& to) const override;
        bool FindGroundPath(Bots::Position const& from, Bots::Position const& to,
            std::vector<Bots::Position>& path) const override;
        std::vector<Bots::NearEnemy> GetEnemiesNear(float yards) const override;
        Bots::RangedWeapon GetRangedWeapon() const override;
        Bots::Guid GetShotTarget() const override;
        uint32 GetLevel(Bots::Guid unit) const override;
        bool IsMoving(Bots::Guid unit) const override;
        bool HasRoomFor(Bots::AmmoTopUp const& topUp) const override;
        bool IsMeleeAttacking() const override;
        // jvbastel/WOWAI#189: the selection of a player, the victim of a creature (BotView.h).
        Bots::Guid GetTargetOf(Bots::Guid unit) const override;
        // jvbastel/WOWAI#197: the last role choice message of the player (BotMgr::RoleChoiceOf).
        Bots::RoleChoice GetRoleChoice(Bots::Guid member) const override;

        // BotActions
        void CastSpell(Bots::SpellId spell, Bots::Guid target) override;
        void MoveTo(Bots::Position const& destination) override;
        void KeepMovingTo(Bots::Position const& destination) override;
        void Attack(Bots::Guid target) override;
        void StopAttack() override;
        void Chase(Bots::Guid target) override;
        void ChaseBehind(Bots::Guid target) override;
        void ChaseToRange(Bots::Guid target, float yards) override;
        void Stand() override;
        void PetAttack(Bots::Guid target) override;
        void PetStop() override;
        void Follow(Bots::Guid unit) override;
        void Loot(bool mayMove) override;
        void Release() override;
        void WalkAsGhost() override;
        void Eat() override;
        void Drink() override;
        void SayParty(std::string const& text, Bots::PartyLine kind) override;
        void StopShot() override;
        void TopUpAmmo(Bots::AmmoTopUp const& topUp) override;

    private:
        // The unit on the map of the bot, in the world, or null.
        Unit* Find(Bots::Guid unit) const;
        // Like Find, and a player that is not on the map of the bot, for
        // example the leader at a graveyard: the player is on another map
        // thread, so only read it.
        Unit const* FindAnywhere(Bots::Guid unit) const;
        // Sits and uses the food or drink item `entry`, refilled silently.
        void UseRestItem(uint32 entry);

        Player* me;
        BotLoot* m_loot;
};

#endif
