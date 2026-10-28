#include "MageModule.h"
#include "CommonRules.h"
#include "DamageRules.h"
#include "StepBack.h"

#include <iterator>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace Bots
{
    namespace Mage
    {
        namespace
        {
            // jvbastel/WOWAI#143: the peel nova. Frost Nova freezes the
            // enemies within this distance of the Mage.
            float const FrostNovaYards = 10.0f;
            // The Mage goes for a peel nova on an enemy at most this far.
            float const PeelReachYards = 15.0f;
            // It runs to this distance of a farther enemy: inside nova range.
            float const NovaCloseInYards = 8.0f;
            // The tank's chance to Taunt an enemy off another ranged member.
            std::int64_t const TauntChanceMs = 1500;
            // An enemy not seen on its victim for this long starts a new
            // wait when it comes back. Longer than a think in the fight
            // (ThinkTimer.h), also when a rule above the peel nova fires
            // for a while and the Mage does not look.
            std::int64_t const ForgetHitMs = 3000;
            // Mana for Evocation below it, in percent.
            std::uint32_t const LowManaPercent = 15;
            // Health of the focus target for Fire Blast below it, in percent.
            std::uint32_t const FinishHealthPercent = 25;

            Guid Self(BotContext const& context)
            {
                return context.view.GetSelf();
            }

            bool InCombat(BotContext const& context)
            {
                return context.view.IsInCombat(Self(context));
            }

            bool OutOfCombat(BotContext const& context)
            {
                return !InCombat(context);
            }

            bool IsAttacked(BotContext const& context)
            {
                return !context.view.GetAttackers(Self(context)).empty();
            }

            bool HasArmor(BotContext const& context)
            {
                return context.view.HasAura(Self(context), FrostArmor) || context.view.HasAura(Self(context), IceArmor);
            }

            // When the Mage first and last saw each loose enemy on another
            // ranged member, by (enemy, member).
            struct HitSeen
            {
                std::int64_t firstMs;
                std::int64_t lastMs;
            };
            using HitsSeen = std::map<std::pair<Guid, Guid>, HitSeen>;
            using HitsSeenPtr = std::shared_ptr<HitsSeen>;

            // The members whose loose enemies the Mage peels, first first:
            // the healer, the Mage, then each other ranged member. Never the
            // tank, nor a melee member. Roles from the group
            // (GroupCoordinator::GetHealer, HasRole).
            std::vector<Guid> PeelVictims(BotContext const& context)
            {
                Guid const self = Self(context);
                Guid const healer = context.group.GetHealer();
                Guid const tank = context.group.GetTank();
                std::vector<Guid> victims;
                if (healer != self && healer != tank && IsGroupMember(context.view, healer)
                    && IsAlive(context.view, healer))
                    victims.push_back(healer);
                victims.push_back(self);
                for (Guid member : context.view.GetGroupMembers())
                    if (member != self && member != healer && member != tank && IsAlive(context.view, member)
                        && context.group.HasRole(member, BotRole::Ranged))
                        victims.push_back(member);
                return victims;
            }

            // The loose enemy to peel now, or NoGuid: the first in the
            // order of PeelVictims that is in melee of its victim, not
            // frozen yet, and at most PeelReachYards from the Mage. An enemy
            // on another ranged member waits TauntChanceMs on it first.
            // Notes the hits on other ranged members in `seen`.
            Guid PickPeelEnemy(BotContext const& context, HitsSeen& seen)
            {
                Guid const self = Self(context);
                Guid const healer = context.group.GetHealer();
                std::int64_t const now = context.nowMs;
                for (auto it = seen.begin(); it != seen.end();)
                    it = now - it->second.lastMs > ForgetHitMs ? seen.erase(it) : std::next(it);
                Guid picked = NoGuid;
                for (Guid victim : PeelVictims(context))
                    for (Guid enemy : EnemiesHittingInMelee(context.view, victim))
                    {
                        bool due = true;
                        if (victim != self && victim != healer)
                        {
                            auto const at = seen.emplace(std::make_pair(enemy, victim), HitSeen{now, now}).first;
                            at->second.lastMs = now;
                            due = now - at->second.firstMs >= TauntChanceMs;
                        }
                        if (picked == NoGuid && due && !IsFrozen(context.view, enemy)
                            && context.view.GetDistance(enemy) <= PeelReachYards)
                            picked = enemy;
                    }
                return picked;
            }

            // The peel nova: Frost Nova on the loose enemy of PickPeelEnemy,
            // after a run into nova range when it is farther. The step back
            // after it is a rule of its own (jvbastel/WOWAI#144). Without a
            // castable Frost Nova (cooldown, unknown) the Mage does nothing
            // new and does not move.
            Rule PeelNovaRule(int priority)
            {
                HitsSeenPtr const seen = std::make_shared<HitsSeen>();
                Rule rule;
                rule.name = "peel nova";
                rule.priority = priority;
                rule.trigger = [seen](BotContext const& context) { return PickPeelEnemy(context, *seen) != NoGuid; };
                rule.action.canDo = [](BotContext const& context) {
                    return CastableRank(context.view, FrostNova, Self(context)) != NoSpell;
                };
                rule.action.run = [seen](BotContext const& context) {
                    Guid const enemy = PickPeelEnemy(context, *seen);
                    if (context.view.GetDistance(enemy) <= FrostNovaYards)
                        context.actions.CastSpell(CastableRank(context.view, FrostNova, Self(context)), Self(context));
                    else
                        context.actions.ChaseToRange(enemy, NovaCloseInYards);
                };
                return rule;
            }
        }

        std::shared_ptr<Strategy const> MakeFrostStrategy()
        {
            int const P = Priority::Class;
            std::vector<Rule> rules = {
                // Defense: the tank holds the threat, but an enemy can still
                // come to the Mage, the healer, or another ranged member.
                PeelNovaRule(P + 90),
                StepBackRule(P + 89, BotRole::Ranged),
                CastRule("ice barrier", P + 85,
                    [](BotContext const& context) {
                        return IsAttacked(context) && !context.view.HasAura(Self(context), IceBarrier);
                    },
                    IceBarrier, PickSelf),
                CastRule("evocation", P + 80,
                    [](BotContext const& context) {
                        Power const mana = context.view.GetPower(Self(context));
                        return InCombat(context) && Percent(mana.current, mana.max) < LowManaPercent;
                    },
                    Evocation, PickSelf),

                // The damage on the focus target.
                CloseInRule(P + 70),
                CastRule("fire blast", P + 65,
                    [](BotContext const& context) {
                        Health const health = context.view.GetHealth(PickDamageTarget(context));
                        return HasDamageTarget(context) && Percent(health.current, health.max) <= FinishHealthPercent;
                    },
                    FireBlast, PickDamageTarget),
                CastRule("frostbolt", P + 60, HasDamageTarget, Frostbolt, PickDamageTarget),
                // Below level 4, and against a target that Frostbolt cannot
                // hit, for example one immune to frost.
                CastRule("fireball", P + 50, HasDamageTarget, Fireball, PickDamageTarget),

                // The buffs, out of combat. The Mage buffs only itself.
                CastRule("ice armor", P + 30,
                    [](BotContext const& context) { return OutOfCombat(context) && !HasArmor(context); },
                    IceArmor, PickSelf),
                CastRule("frost armor", P + 29,
                    [](BotContext const& context) {
                        return OutOfCombat(context) && !HasArmor(context) && !context.view.KnowsSpell(IceArmor);
                    },
                    FrostArmor, PickSelf),
                CastRule("arcane intellect", P + 25,
                    [](BotContext const& context) {
                        return OutOfCombat(context) && !context.view.HasAura(Self(context), ArcaneIntellect);
                    },
                    ArcaneIntellect, PickSelf),
            };
            return std::make_shared<Strategy const>(Strategy{"mage frost", std::move(rules)});
        }
    }
}
