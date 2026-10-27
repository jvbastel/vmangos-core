#include "MageModule.h"
#include "CommonRules.h"
#include "DamageRules.h"

namespace Bots
{
    namespace Mage
    {
        namespace
        {
            // Frost Nova hits the enemies within this distance of the bot.
            float const FrostNovaYards = 8.0f;
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
        }

        std::shared_ptr<Strategy const> MakeFrostStrategy()
        {
            int const P = Priority::Class;
            std::vector<Rule> rules = {
                // Defense: the tank holds the threat, but an enemy can still
                // come to the Mage.
                CastRule("frost nova", P + 90,
                    [](BotContext const& context) { return IsHitInMelee(context, FrostNovaYards); }, FrostNova,
                    PickSelf),
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
