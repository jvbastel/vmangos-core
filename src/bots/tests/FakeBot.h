#ifndef WOWAI_BOTS_TESTS_FAKE_BOT_H
#define WOWAI_BOTS_TESTS_FAKE_BOT_H

#include "BotActions.h"
#include "BotView.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace Bots
{
    namespace Tests
    {
        // The fake adapter of the tests. A test sets the world that the bot
        // sees, and reads the deeds of the bot as text lines, in order.
        class FakeBot : public BotView, public BotActions
        {
        public:
            explicit FakeBot(Guid self) : m_self(self) {}

            void SetHealth(Guid unit, std::uint32_t current, std::uint32_t max)
            {
                m_health[unit] = Health{current, max};
            }
            void SetCurrentCast(Guid unit, SpellId spell) { m_casts[unit] = spell; }
            // Unit `unit` casts spell `spell` of kind `kind`. NoSpell: the
            // cast ends.
            void SetCast(Guid unit, SpellId spell, CastKind kind, bool interruptible = true)
            {
                m_casts[unit] = spell;
                m_castInfo[unit] = spell == NoSpell ? CastInfo{NoSpell, CastKind::None, false} : CastInfo{spell, kind, interruptible};
            }
            // The distance between two units. Unset: the distance of the
            // farther unit from the bot.
            void SetDistanceBetween(Guid a, Guid b, float yards)
            {
                m_between[{a, b}] = yards;
                m_between[{b, a}] = yards;
            }
            void SetDebuff(Guid unit, DispelKind kind, bool has = true)
            {
                if (has)
                    m_debuffs.insert({unit, kind});
                else
                    m_debuffs.erase({unit, kind});
            }
            void SetKnownSpell(SpellId spell) { m_known.insert(spell); }
            // The bot knows the spell, but cannot cast it now (cooldown,
            // range, power, stance).
            void SetCannotCast(SpellId spell) { m_cannotCast.insert(spell); }
            void SetPower(Guid unit, std::uint32_t current, std::uint32_t max) { m_power[unit] = Power{current, max}; }
            void SetDistance(Guid unit, float yards) { m_distance[unit] = yards; }
            void SetInCombat(Guid unit, bool inCombat = true) { m_inCombat[unit] = inCombat; }
            void SetVictim(Guid unit, Guid victim)
            {
                m_victim[unit] = victim;
                m_inCombat[unit] = victim != NoGuid;
            }
            void SetAura(Guid unit, SpellId firstRank, bool has = true)
            {
                if (has)
                    m_auras.insert({unit, firstRank});
                else
                    m_auras.erase({unit, firstRank});
            }
            void SetFollowTarget(Guid unit) { m_followTarget = unit; }
            void SetPet(Guid pet) { m_pet = pet; }
            void SetComboPoints(std::uint32_t points) { m_comboPoints = points; }
            void ClearDeeds() { m_deeds.clear(); }
            // The bot has corpse loot to do. `onlyWithMove`: the corpse is
            // out of reach, so only a bot that may move has work.
            void SetLootWork(bool has, bool onlyWithMove = false)
            {
                m_lootWork = has;
                m_lootOnlyWithMove = onlyWithMove;
            }
            // The ranks of one spell, first rank first.
            void SetSpellChain(std::vector<SpellId> const& ranks)
            {
                for (std::size_t i = 0; i + 1 < ranks.size(); ++i)
                    m_nextRank[ranks[i]] = ranks[i + 1];
            }

            // The other members of the bot's group, in order.
            void SetGroupMembers(std::vector<Guid> const& others) { m_groupMembers = others; }

            void SetEntry(Guid unit, std::uint32_t entry) { m_entry[unit] = entry; }
            // A unit with a position and no distance of its own (SetDistance)
            // is as far from the bot as their positions are.
            void SetPosition(Guid unit, Position position) { m_position[unit] = position; }
            // A wall along the line x = `x` (or y = `y`), from top to bottom.
            void AddWallAtX(float x) { m_wallsAtX.push_back(x); }
            void AddWallAtY(float y) { m_wallsAtY.push_back(y); }
            void SetLifeState(Guid unit, LifeState state) { m_lifeState[unit] = state; }
            void SetGroupLeader(Guid unit) { m_leader = unit; }
            void SetCorpseDistance(Guid unit, float yards) { m_corpseDistance[unit] = yards; }
            void SetCanResurrect(Guid unit, bool can = true)
            {
                if (can)
                    m_resurrecters.insert(unit);
                else
                    m_resurrecters.erase(unit);
            }
            void SetMana(Guid unit, std::uint32_t current, std::uint32_t max) { m_mana[unit] = Power{current, max}; }
            void SetEating(bool eating = true) { m_eating = eating; }
            void SetDrinking(bool drinking = true) { m_drinking = drinking; }
            // Spell `spell` targets dead units, for example a resurrect.
            void SetCastsOnDead(SpellId spell) { m_castsOnDead.insert(spell); }

            std::vector<std::string> const& Deeds() const { return m_deeds; }

            // BotView
            Guid GetSelf() const override { return m_self; }
            Health GetHealth(Guid unit) const override
            {
                auto found = m_health.find(unit);
                return found == m_health.end() ? Health{0, 0} : found->second;
            }
            SpellId GetCurrentCast(Guid unit) const override
            {
                auto found = m_casts.find(unit);
                return found == m_casts.end() ? NoSpell : found->second;
            }
            CastInfo GetCast(Guid unit) const override
            {
                auto found = m_castInfo.find(unit);
                return found == m_castInfo.end() ? CastInfo{NoSpell, CastKind::None, false} : found->second;
            }

            Power GetPower(Guid unit) const override
            {
                auto found = m_power.find(unit);
                return found == m_power.end() ? Power{0, 0} : found->second;
            }
            float GetDistance(Guid unit) const override
            {
                auto found = m_distance.find(unit);
                if (found != m_distance.end())
                    return found->second;
                auto here = m_position.find(m_self);
                auto there = m_position.find(unit);
                if (here != m_position.end() && there != m_position.end())
                    return std::hypot(there->second.x - here->second.x, there->second.y - here->second.y);
                return unit == m_self || GetHealth(unit).max ? 5.0f : 100000.0f;
            }
            float GetDistanceBetween(Guid a, Guid b) const override
            {
                auto found = m_between.find({a, b});
                if (found != m_between.end())
                    return found->second;
                return a == b ? 0.0f : std::max(GetDistance(a), GetDistance(b));
            }
            bool IsInCombat(Guid unit) const override
            {
                auto found = m_inCombat.find(unit);
                return found != m_inCombat.end() && found->second;
            }
            Guid GetVictim(Guid unit) const override
            {
                auto found = m_victim.find(unit);
                return found == m_victim.end() ? NoGuid : found->second;
            }
            std::vector<Guid> GetAttackers(Guid unit) const override
            {
                std::vector<Guid> attackers;
                for (auto const& pair : m_victim)
                    if (pair.second == unit && unit != NoGuid)
                        attackers.push_back(pair.first);
                return attackers;
            }
            bool HasAura(Guid unit, SpellId firstRank) const override { return m_auras.count({unit, firstRank}) != 0; }
            bool HasDebuff(Guid unit, DispelKind kind) const override { return m_debuffs.count({unit, kind}) != 0; }
            Guid GetFollowTarget() const override { return m_followTarget; }
            ChaseState GetChase() const override { return m_chase; }
            Guid GetPet() const override { return m_pet; }
            std::uint32_t GetComboPoints() const override { return m_comboPoints; }
            bool HasLootWork(bool mayMove) const override { return m_lootWork && (mayMove || !m_lootOnlyWithMove); }
            bool CanCast(SpellId spell, Guid target) const override
            {
                if (!KnowsSpell(spell) || m_cannotCast.count(spell))
                    return false;
                if (m_castsOnDead.count(spell))
                    return GetLifeState(target) == LifeState::Dead;
                return GetHealth(target).current > 0 && GetLifeState(target) == LifeState::Alive;
            }
            bool KnowsSpell(SpellId spell) const override { return m_known.count(spell) != 0; }
            SpellId GetNextRank(SpellId spell) const override
            {
                auto found = m_nextRank.find(spell);
                return found == m_nextRank.end() ? NoSpell : found->second;
            }

            std::vector<Guid> GetGroupMembers() const override
            {
                std::vector<Guid> members{m_self};
                members.insert(members.end(), m_groupMembers.begin(), m_groupMembers.end());
                return members;
            }

            std::uint32_t GetEntry(Guid unit) const override
            {
                auto found = m_entry.find(unit);
                return found == m_entry.end() ? 0 : found->second;
            }
            Position GetPosition(Guid unit) const override
            {
                auto found = m_position.find(unit);
                return found == m_position.end() ? Position{0, 0, 0} : found->second;
            }
            float GetWallDistance(Position const& from, float angle, float maxYards) const override
            {
                float nearest = maxYards;
                float const dx = std::cos(angle);
                float const dy = std::sin(angle);
                for (float x : m_wallsAtX)
                    if (std::fabs(dx) > 1e-6f && (x - from.x) / dx >= 0)
                        nearest = std::min(nearest, (x - from.x) / dx);
                for (float y : m_wallsAtY)
                    if (std::fabs(dy) > 1e-6f && (y - from.y) / dy >= 0)
                        nearest = std::min(nearest, (y - from.y) / dy);
                return nearest;
            }
            LifeState GetLifeState(Guid unit) const override
            {
                auto found = m_lifeState.find(unit);
                return found == m_lifeState.end() ? LifeState::Alive : found->second;
            }
            Guid GetGroupLeader() const override { return m_leader; }
            float GetCorpseDistance(Guid unit) const override
            {
                auto found = m_corpseDistance.find(unit);
                return found == m_corpseDistance.end() ? 100000.0f : found->second;
            }
            bool CanResurrect(Guid unit) const override
            {
                return m_resurrecters.count(unit) != 0 && GetLifeState(unit) == LifeState::Alive;
            }
            Power GetMana(Guid unit) const override
            {
                auto found = m_mana.find(unit);
                return found == m_mana.end() ? Power{0, 0} : found->second;
            }
            bool IsEating() const override { return m_eating; }
            bool IsDrinking() const override { return m_drinking; }

            // BotActions
            void CastSpell(SpellId spell, Guid target) override
            {
                std::ostringstream deed;
                deed << "cast " << spell << " on 0x" << std::hex << std::setw(16) << std::setfill('0') << target;
                m_deeds.push_back(deed.str());
            }
            void MoveTo(Position const& destination) override
            {
                std::ostringstream deed;
                // To 0.01 yards, without float noise such as -0.
                auto yards = [](float value) { return std::round(value * 100.0f) / 100.0f + 0.0f; };
                deed << "move to " << yards(destination.x) << ' ' << yards(destination.y) << ' '
                     << yards(destination.z);
                m_deeds.push_back(deed.str());
            }

            void Attack(Guid target) override { AddDeed("attack", target); }
            void Chase(Guid target) override
            {
                AddDeed("chase", target);
                m_chase = {target, false};
            }
            void ChaseBehind(Guid target) override
            {
                AddDeed("chase behind", target);
                m_chase = {target, true};
            }
            void ChaseToRange(Guid target, float yards) override
            {
                AddDeed("chase", target);
                std::ostringstream range;
                range << " to " << yards << " yards";
                m_deeds.back() += range.str();
                m_chase = {target, false};
            }
            void PetAttack(Guid target) override { AddDeed("pet attack", target); }
            void Follow(Guid unit) override
            {
                AddDeed("follow", unit);
                m_chase = {NoGuid, false};
            }
            void Loot(bool mayMove) override { m_deeds.push_back(mayMove ? "loot" : "loot in place"); }
            void Release() override { m_deeds.push_back("release"); }
            void ReviveAtCorpse() override { m_deeds.push_back("revive at the corpse"); }
            void Eat() override { m_deeds.push_back("eat"); }
            void Drink() override { m_deeds.push_back("drink"); }

        private:
            void AddDeed(char const* verb, Guid unit)
            {
                std::ostringstream deed;
                deed << verb << " 0x" << std::hex << std::setw(16) << std::setfill('0') << unit;
                m_deeds.push_back(deed.str());
            }

            Guid m_self;
            std::map<Guid, Health> m_health;
            std::map<Guid, SpellId> m_casts;
            std::map<Guid, CastInfo> m_castInfo;
            std::map<std::pair<Guid, Guid>, float> m_between;
            std::set<std::pair<Guid, DispelKind>> m_debuffs;
            std::set<SpellId> m_known;
            std::map<SpellId, SpellId> m_nextRank;
            std::set<SpellId> m_cannotCast;
            std::map<Guid, Power> m_power;
            std::map<Guid, float> m_distance;
            std::map<Guid, bool> m_inCombat;
            std::map<Guid, Guid> m_victim;
            std::set<std::pair<Guid, SpellId>> m_auras;
            Guid m_followTarget = NoGuid;
            std::vector<Guid> m_groupMembers;
            ChaseState m_chase = {NoGuid, false};
            std::uint32_t m_comboPoints = 0;
            Guid m_pet = NoGuid;
            bool m_lootWork = false;
            bool m_lootOnlyWithMove = false;
            std::map<Guid, std::uint32_t> m_entry;
            std::map<Guid, Position> m_position;
            std::vector<float> m_wallsAtX;
            std::vector<float> m_wallsAtY;
            std::map<Guid, LifeState> m_lifeState;
            Guid m_leader = NoGuid;
            std::map<Guid, float> m_corpseDistance;
            std::set<Guid> m_resurrecters;
            std::map<Guid, Power> m_mana;
            bool m_eating = false;
            bool m_drinking = false;
            std::set<SpellId> m_castsOnDead;
            std::vector<std::string> m_deeds;
        };
    }
}

#endif
