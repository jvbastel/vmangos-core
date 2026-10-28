#include "RogueModule.h"
#include "CommonRules.h"
#include "DamageRules.h"

namespace Bots
{
    namespace Rogue
    {
        namespace
        {
            // Combo points for Slice and Dice, and for Eviscerate.
            std::uint32_t const SliceComboPoints = 2;
            std::uint32_t const EviscerateComboPoints = 5;
            // Health of the target at or below it, in percent: the target dies
            // soon, so the bot spends the combo points it has.
            std::uint32_t const DyingHealthPercent = 25;

            Guid Self(BotContext const& context)
            {
                return context.view.GetSelf();
            }

            // The bot fights the damage target with its melee attack.
            bool Fights(BotContext const& context)
            {
                Guid const target = PickMeleeDamageTarget(context);
                return target != NoGuid && context.view.GetVictim(Self(context)) == target;
            }

            // The bot can stand behind the target: the target attacks
            // another unit, so it does not turn to the bot.
            bool CanStandBehind(BotContext const& context, Guid target)
            {
                return context.view.GetVictim(target) != Self(context);
            }

            // Five combo points, or any combo points on a dying target.
            bool ReadyToEviscerate(BotContext const& context)
            {
                std::uint32_t const comboPoints = context.view.GetComboPoints();
                if (comboPoints >= EviscerateComboPoints)
                    return true;
                Health const health = context.view.GetHealth(PickMeleeDamageTarget(context));
                return comboPoints > 0 && Percent(health.current, health.max) <= DyingHealthPercent;
            }

            void ChaseTarget(BotContext const& context, Guid target)
            {
                if (CanStandBehind(context, target))
                    context.actions.ChaseBehind(target);
                else
                    context.actions.Chase(target);
            }

            // jvbastel/WOWAI#194: the bot swings at the focus, and holds
            // back on its threat now (HoldsBackOnThreat, DamageRules.h): the
            // melee damage target is no longer the focus.
            bool AtTheThreatStop(BotContext const& context)
            {
                Guid const victim = context.view.GetVictim(Self(context));
                return victim != NoGuid && victim == context.group.GetFocusTarget()
                    && PickMeleeDamageTarget(context) != victim
                    && context.group.HoldsBackOnThreat(Self(context), victim);
            }

            Guid PickVictim(BotContext const& context)
            {
                return context.view.GetVictim(Self(context));
            }

            Rule MoveRule(std::string name, int priority, Trigger trigger)
            {
                Rule rule;
                rule.name = std::move(name);
                rule.priority = priority;
                rule.trigger = std::move(trigger);
                rule.action.canDo = [](BotContext const&) { return true; };
                rule.action.run = [](BotContext const& context) { ChaseTarget(context, PickMeleeDamageTarget(context)); };
                return rule;
            }
        }

        std::shared_ptr<Strategy const> MakeMeleeStrategy()
        {
            int const P = Priority::Class;
            std::vector<Rule> rules = {
                // Final review of spec #142, finding 2: the bot swings at an
                // enemy that it now holds fire on (HoldsFireOn: frozen, and
                // not the tank's): the running swing would break the
                // freeze. The player's attack target goes on (it is the
                // damage target then). Another target follows at P + 80.
                Rule{"stop the swing on a frozen enemy", P + 81,
                    [](BotContext const& context) {
                        Guid const victim = context.view.GetVictim(Self(context));
                        return victim != NoGuid && context.view.IsMeleeAttacking() && HoldsFireOn(context, victim)
                            && PickMeleeDamageTarget(context) != victim;
                    },
                    {[](BotContext const&) { return true; },
                        [](BotContext const& context) { context.actions.StopAttack(); }}},

                // jvbastel/WOWAI#194: at the threat stop, Feint when the
                // bot can: its threat falls, and it may go on. Else it
                // stops the swing until its threat falls under the start
                // level.
                CastRule("feint at the threat stop", P + 83, AtTheThreatStop, Feint, PickVictim),
                Rule{"stop the swing at the threat stop", P + 82,
                    [](BotContext const& context) {
                        return context.view.IsMeleeAttacking() && AtTheThreatStop(context);
                    },
                    {[](BotContext const&) { return true; },
                        [](BotContext const& context) { context.actions.StopAttack(); }}},

                // The damage target changes, or the fight starts.
                Rule{"attack the target", P + 80,
                    [](BotContext const& context) {
                        Guid const target = PickMeleeDamageTarget(context);
                        return target != NoGuid && context.view.GetVictim(Self(context)) != target;
                    },
                    {[](BotContext const&) { return true; },
                        [](BotContext const& context) {
                            Guid const target = PickMeleeDamageTarget(context);
                            context.actions.Attack(target);
                            ChaseTarget(context, target);
                        }}},

                // Behind the target when it can: no parry and no block.
                MoveRule("stand behind the target", P + 75,
                    [](BotContext const& context) {
                        Guid const target = PickMeleeDamageTarget(context);
                        ChaseState const chase = context.view.GetChase();
                        return Fights(context) && CanStandBehind(context, target)
                            && !(chase.target == target && chase.behind);
                    }),
                MoveRule("face the target", P + 74,
                    [](BotContext const& context) {
                        Guid const target = PickMeleeDamageTarget(context);
                        ChaseState const chase = context.view.GetChase();
                        return Fights(context) && !CanStandBehind(context, target)
                            && !(chase.target == target && !chase.behind);
                    }),

                CastRule("eviscerate", P + 60,
                    [](BotContext const& context) {
                        return Fights(context) && ReadyToEviscerate(context);
                    },
                    Eviscerate, PickMeleeDamageTarget),
                // On the target: the spell takes the combo points on it.
                CastRule("slice and dice", P + 55,
                    [](BotContext const& context) {
                        return Fights(context) && context.view.GetComboPoints() >= SliceComboPoints
                            && !context.view.HasAura(Self(context), SliceAndDice);
                    },
                    SliceAndDice, PickMeleeDamageTarget),
                CastRule("sinister strike", P + 50, Fights, SinisterStrike, PickMeleeDamageTarget),
            };
            return std::make_shared<Strategy const>(Strategy{"rogue melee", std::move(rules)});
        }
    }
}
