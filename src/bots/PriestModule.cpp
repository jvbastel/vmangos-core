#include "PriestModule.h"
#include "CommonRules.h"

namespace Bots
{
    namespace Priest
    {
        namespace
        {
            // Health fractions: a member below one of them needs that heal.
            float const ShieldBelow = 0.3f;
            float const HealBelow = 0.5f;
            float const LesserHealBelow = 0.7f;
            float const RenewBelow = 0.9f;

            using MemberFilter = bool (*)(BotView const& view, Guid member);

            bool AnyMember(BotView const&, Guid)
            {
                return true;
            }

            bool CanTakeShield(BotView const& view, Guid member)
            {
                return !view.HasAura(member, WeakenedSoul) && !view.HasAura(member, PowerWordShield);
            }

            bool HasNoRenew(BotView const& view, Guid member)
            {
                return !view.HasAura(member, Renew);
            }

            float HealthFraction(BotView const& view, Guid unit)
            {
                Health const health = view.GetHealth(unit);
                return health.max ? float(health.current) / float(health.max) : 1.0f;
            }

            // The living group member with the lowest health fraction below
            // `below` that `filter` accepts, or NoGuid. On a tie, the first
            // member.
            Guid MostHurtMember(BotContext const& context, float below, MemberFilter filter)
            {
                Guid most = NoGuid;
                float lowest = below;
                for (Guid member : context.view.GetGroupMembers())
                {
                    float const fraction = HealthFraction(context.view, member);
                    if (IsAlive(context.view, member) && fraction < lowest && filter(context.view, member))
                    {
                        most = member;
                        lowest = fraction;
                    }
                }
                return most;
            }

            TargetPicker HurtMember(float below, MemberFilter filter = AnyMember)
            {
                return [below, filter](BotContext const& context) { return MostHurtMember(context, below, filter); };
            }

            Trigger SomeoneBelow(float below, MemberFilter filter = AnyMember)
            {
                return [below, filter](BotContext const& context) {
                    return MostHurtMember(context, below, filter) != NoGuid;
                };
            }
        }

        std::shared_ptr<Strategy const> MakeHealerStrategy()
        {
            int const P = Priority::Class;
            std::vector<Rule> rules = {
                // The heals, the most urgent first. Each picks the most
                // hurt member that needs it. At level 16 the Priest knows no
                // dispel: it heals through every debuff.
                CastRule("shield a member near death", P + 85, SomeoneBelow(ShieldBelow, CanTakeShield),
                    PowerWordShield, HurtMember(ShieldBelow, CanTakeShield)),
                CastRule("heal", P + 80, SomeoneBelow(HealBelow), Heal, HurtMember(HealBelow)),
                CastRule("lesser heal a badly hurt member", P + 75, SomeoneBelow(HealBelow), LesserHeal, HurtMember(HealBelow)),
                CastRule("renew", P + 70, SomeoneBelow(RenewBelow, HasNoRenew), Renew, HurtMember(RenewBelow, HasNoRenew)),
                CastRule("lesser heal", P + 65, SomeoneBelow(LesserHealBelow), LesserHeal, HurtMember(LesserHealBelow)),
            };
            return std::make_shared<Strategy const>(Strategy{"priest healer", std::move(rules)});
        }
    }
}
