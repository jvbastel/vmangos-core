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
            // Health of the target below it, in percent: the target dies
            // soon, so the bot spends the combo points it has.
            std::uint32_t const DyingHealthPercent = 25;

            Guid Self(BotContext const& context)
            {
                return context.view.GetSelf();
            }

            // The bot fights the damage target with its melee attack.
            bool Fights(BotContext const& context)
            {
                Guid const target = PickDamageTarget(context);
                return target != NoGuid && context.view.GetVictim(Self(context)) == target;
            }

            // The bot can stand behind the target: the target attacks
            // another unit, so it does not turn to the bot.
            bool CanStandBehind(BotContext const& context, Guid target)
            {
                return context.view.GetVictim(target) != Self(context);
            }

            std::uint32_t Percent(std::uint32_t current, std::uint32_t max)
            {
                return max == 0 ? 0 : current * 100 / max;
            }

            // Five combo points, or any combo points on a dying target.
            bool ReadyToEviscerate(BotContext const& context)
            {
                std::uint32_t const comboPoints = context.view.GetComboPoints();
                if (comboPoints >= EviscerateComboPoints)
                    return true;
                Health const health = context.view.GetHealth(PickDamageTarget(context));
                return comboPoints > 0 && Percent(health.current, health.max) <= DyingHealthPercent;
            }

            void ChaseTarget(BotContext const& context, Guid target)
            {
                if (CanStandBehind(context, target))
                    context.actions.ChaseBehind(target);
                else
                    context.actions.Chase(target);
            }

            Rule MoveRule(std::string name, int priority, Trigger trigger)
            {
                Rule rule;
                rule.name = std::move(name);
                rule.priority = priority;
                rule.trigger = std::move(trigger);
                rule.action.canDo = [](BotContext const&) { return true; };
                rule.action.run = [](BotContext const& context) { ChaseTarget(context, PickDamageTarget(context)); };
                return rule;
            }
        }

        std::shared_ptr<Strategy const> MakeMeleeStrategy()
        {
            int const P = Priority::Class;
            std::vector<Rule> rules = {
                // The damage target changes, or the fight starts.
                Rule{"attack the target", P + 80,
                    [](BotContext const& context) {
                        Guid const target = PickDamageTarget(context);
                        return target != NoGuid && context.view.GetVictim(Self(context)) != target;
                    },
                    {[](BotContext const&) { return true; },
                        [](BotContext const& context) {
                            Guid const target = PickDamageTarget(context);
                            context.actions.Attack(target);
                            ChaseTarget(context, target);
                        }}},

                // Behind the target when it can: no parry and no block.
                MoveRule("stand behind the target", P + 75,
                    [](BotContext const& context) {
                        Guid const target = PickDamageTarget(context);
                        ChaseState const chase = context.view.GetChase();
                        return Fights(context) && CanStandBehind(context, target)
                            && !(chase.target == target && chase.behind);
                    }),
                MoveRule("face the target", P + 74,
                    [](BotContext const& context) {
                        Guid const target = PickDamageTarget(context);
                        ChaseState const chase = context.view.GetChase();
                        return Fights(context) && !CanStandBehind(context, target)
                            && !(chase.target == target && !chase.behind);
                    }),

                CastRule("eviscerate", P + 60,
                    [](BotContext const& context) {
                        return Fights(context) && ReadyToEviscerate(context);
                    },
                    Eviscerate, PickDamageTarget),
                // On the target: the spell takes the combo points on it.
                CastRule("slice and dice", P + 55,
                    [](BotContext const& context) {
                        return Fights(context) && context.view.GetComboPoints() >= SliceComboPoints
                            && !context.view.HasAura(Self(context), SliceAndDice);
                    },
                    SliceAndDice, PickDamageTarget),
                CastRule("sinister strike", P + 50, Fights, SinisterStrike, PickDamageTarget),
            };
            return std::make_shared<Strategy const>(Strategy{"rogue melee", std::move(rules)});
        }
    }
}
