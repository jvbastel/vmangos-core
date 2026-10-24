#include "WarlockModule.h"
#include "CommonRules.h"

namespace Bots
{
    namespace Warlock
    {
        namespace
        {
            // The Warlock stands this far from its target: in the range of
            // its spells (30 yards), and out of the melee.
            float const RangeYards = 25.0f;
            // Life Tap below this mana, and above this health, in percent.
            std::uint32_t const LowManaPercent = 25;
            std::uint32_t const SpareHealthPercent = 60;

            Guid Self(BotContext const& context)
            {
                return context.view.GetSelf();
            }

            bool OutOfCombat(BotContext const& context)
            {
                return !context.view.IsInCombat(Self(context));
            }

            bool HasTarget(BotContext const& context)
            {
                return PickDamageTarget(context) != NoGuid;
            }

            std::uint32_t Percent(std::uint32_t current, std::uint32_t max)
            {
                return max ? current * 100 / max : 0;
            }

            // A damage over time spell that the target does not have yet.
            Rule DotRule(std::string name, int priority, SpellId firstRank)
            {
                return CastRule(std::move(name), priority,
                    [firstRank](BotContext const& context) {
                        return HasTarget(context) && !context.view.HasAura(PickDamageTarget(context), firstRank);
                    },
                    firstRank, PickDamageTarget);
            }
        }

        std::shared_ptr<Strategy const> MakeRangedStrategy()
        {
            int const P = Priority::Class;
            std::vector<Rule> rules = {
                // Out of combat: the Imp and the armor.
                CastRule("summon the imp", P + 90,
                    [](BotContext const& context) {
                        return OutOfCombat(context) && !IsAlive(context.view, context.view.GetPet());
                    },
                    SummonImp, PickSelf),
                CastRule("demon armor", P + 85,
                    [](BotContext const& context) {
                        return OutOfCombat(context) && !context.view.HasAura(Self(context), DemonArmor);
                    },
                    DemonArmor, PickSelf),
                CastRule("demon skin", P + 84,
                    [](BotContext const& context) {
                        return OutOfCombat(context) && !context.view.KnowsSpell(DemonArmor)
                            && !context.view.HasAura(Self(context), DemonSkin);
                    },
                    DemonSkin, PickSelf),

                // The fight: the Imp attacks the damage target, and the
                // Warlock stands in range of it.
                Rule{"send the imp", P + 80,
                    [](BotContext const& context) {
                        Guid const pet = context.view.GetPet();
                        return HasTarget(context) && context.view.GetVictim(pet) != PickDamageTarget(context);
                    },
                    {[](BotContext const& context) { return IsAlive(context.view, context.view.GetPet()); },
                        [](BotContext const& context) { context.actions.PetAttack(PickDamageTarget(context)); }}},
                Rule{"get in range", P + 75,
                    [](BotContext const& context) {
                        return HasTarget(context) && context.view.GetChase().target != PickDamageTarget(context);
                    },
                    {[](BotContext const&) { return true; },
                        [](BotContext const& context) {
                            context.actions.ChaseToRange(PickDamageTarget(context), RangeYards);
                        }}},

                CastRule("life tap", P + 60,
                    [](BotContext const& context) {
                        Power const mana = context.view.GetPower(Self(context));
                        Health const health = context.view.GetHealth(Self(context));
                        return HasTarget(context) && Percent(mana.current, mana.max) < LowManaPercent
                            && Percent(health.current, health.max) > SpareHealthPercent;
                    },
                    LifeTap, PickSelf),
                DotRule("corruption", P + 55, Corruption),
                DotRule("curse of agony", P + 50, CurseOfAgony),
                DotRule("immolate", P + 45, Immolate),
                CastRule("shadow bolt", P + 40, HasTarget, ShadowBolt, PickDamageTarget),
            };
            return std::make_shared<Strategy const>(Strategy{"warlock ranged", std::move(rules)});
        }
    }
}
