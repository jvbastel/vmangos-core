#include "WarriorModule.h"
#include "CommonRules.h"
#include "GroupRules.h"
#include "SpellBook.h"

namespace Bots
{
    namespace Warrior
    {
        namespace
        {
            // Rage for Bloodrage below it, and for Heroic Strike from it.
            std::uint32_t const LowRage = 20;
            std::uint32_t const SpareRage = 45;
            // The range of Taunt.
            float const TauntYards = 20.0f;

            Guid Self(BotContext const& context)
            {
                return context.view.GetSelf();
            }

            bool InCombat(BotContext const& context)
            {
                return context.view.IsInCombat(Self(context));
            }

            std::uint32_t Rage(BotContext const& context)
            {
                return context.view.GetPower(Self(context)).current;
            }

            // The bot pulls: it is the puller, and the target does not
            // attack it yet.
            bool IsPulling(BotContext const& context)
            {
                PullState const pull = context.group.GetPull();
                return pull.phase == PullPhase::Pulling && pull.puller == Self(context);
            }

            // The fight on the focus target goes on: the bot is in combat,
            // and the focus target lives.
            bool FightsFocus(BotContext const& context)
            {
                return InCombat(context) && IsAlive(context.view, context.group.GetFocusTarget());
            }

            // The enemy to fight back: a living attacker of the bot first,
            // else a living attacker of another group member.
            Guid PickFightBackTarget(BotContext const& context)
            {
                return PickMemberAttacker(context);
            }

            // An enemy that hits another group member and is not the focus
            // target: the tank taunts it off.
            Guid PickLooseEnemy(BotContext const& context)
            {
                return PickMemberAttacker(context, Self(context), context.group.GetFocusTarget());
            }

            Rule Bookkeeping(std::string name, int priority, Trigger trigger, std::function<void(BotContext const&)> run)
            {
                Rule rule;
                rule.name = std::move(name);
                rule.priority = priority;
                rule.trigger = std::move(trigger);
                rule.action.canDo = [](BotContext const&) { return true; };
                rule.action.run = std::move(run);
                return rule;
            }
        }

        std::shared_ptr<Strategy const> MakeTankStrategy()
        {
            int const P = Priority::Class;
            std::vector<Rule> rules = {
                // The pull and the fight end when the focus target dies or
                // the bot cannot see it any more.
                Bookkeeping("end the fight", P + 90,
                    [](BotContext const& context) {
                        Guid const focus = context.group.GetFocusTarget();
                        return focus != NoGuid && !IsAlive(context.view, focus);
                    },
                    [](BotContext const& context) { context.group.EndPull(); }),
                Bookkeeping("hold the pull", P + 85,
                    [](BotContext const& context) {
                        return IsPulling(context) && context.view.GetVictim(PickPullTarget(context)) == Self(context);
                    },
                    [](BotContext const& context) { context.group.HoldPull(Self(context)); }),

                // The pull: Charge in Battle Stance, else run to the target.
                CastRule("battle stance to charge", P + 80,
                    [](BotContext const& context) {
                        return IsPulling(context) && !InCombat(context)
                            && !context.view.HasAura(Self(context), BattleStance)
                            && context.view.KnowsSpell(Charge);
                    },
                    BattleStance, PickSelf),
                CastRule("charge the pull target", P + 75,
                    [](BotContext const& context) { return IsPulling(context) && !InCombat(context); },
                    Charge, PickPullTarget),
                // Also when the bot attacks the target but does not chase it:
                // after a charge that started no fight, Charge cannot come
                // again (cooldown, or the target is too near), and the bot
                // would wait for good.
                MeleeRule("run to the pull target", P + 70,
                    [](BotContext const& context) {
                        Guid const target = PickPullTarget(context);
                        return IsPulling(context) && (context.view.GetVictim(Self(context)) != target
                            || (!InCombat(context) && context.view.GetChase().target != target));
                    },
                    PickPullTarget),

                // An enemy attacks the bot or another group member outside
                // a pull: it becomes the focus target. This is no pull of its
                // own: the enemy is in the fight already.
                Bookkeeping("fight back", P + 65,
                    [](BotContext const& context) {
                        return context.group.GetFocusTarget() == NoGuid && PickFightBackTarget(context) != NoGuid;
                    },
                    [](BotContext const& context) { context.group.SetFocusTarget(PickFightBackTarget(context)); }),

                // The focus hits another member, and the taunt cannot come:
                // the tank knows no Taunt, or it cannot cast it in range (it
                // waits for its cooldown, after a resist). The damage bots
                // then attack the focus, or nobody but the tank does.
                Bookkeeping("give up the taunt", P + 64,
                    [](BotContext const& context) {
                        Guid const focus = PickFocusTarget(context);
                        Guid const victim = context.view.GetVictim(focus);
                        if (!FightsFocus(context) || victim == NoGuid || victim == Self(context)
                            || context.group.IsTauntGivenUp(focus))
                            return false;
                        SpellId const taunt = HighestKnownRank(context.view, Taunt);
                        return taunt == NoSpell || (context.view.HasAura(Self(context), DefensiveStance)
                            && context.view.GetDistance(focus) <= TauntYards && !context.view.CanCast(taunt, focus));
                    },
                    [](BotContext const& context) { context.group.GiveUpTaunt(PickFocusTarget(context)); }),

                // The fight on the focus target.
                MeleeRule("attack the focus target", P + 62,
                    [](BotContext const& context) {
                        return FightsFocus(context) && context.view.GetVictim(Self(context)) != PickFocusTarget(context);
                    },
                    PickFocusTarget),
                CastRule("defensive stance", P + 60,
                    [](BotContext const& context) {
                        return FightsFocus(context) && !context.view.HasAura(Self(context), DefensiveStance);
                    },
                    DefensiveStance, PickSelf),
                // Over the attack: a new focus that hits another member is
                // taunted off at once, from up to 20 yards, before the walk
                // to it.
                CastRule("taunt", P + 63,
                    [](BotContext const& context) {
                        Guid const victim = context.view.GetVictim(PickFocusTarget(context));
                        return FightsFocus(context) && victim != NoGuid && victim != Self(context);
                    },
                    Taunt, PickFocusTarget),
                CastRule("taunt an enemy off a member", P + 54,
                    [](BotContext const& context) { return FightsFocus(context) && PickLooseEnemy(context) != NoGuid; },
                    Taunt, PickLooseEnemy),
                CastRule("bloodrage", P + 45,
                    [](BotContext const& context) { return FightsFocus(context) && Rage(context) < LowRage; },
                    Bloodrage, PickSelf),
                CastRule("revenge", P + 40, FightsFocus, Revenge, PickFocusTarget),
                CastRule("heroic strike", P + 35,
                    [](BotContext const& context) { return FightsFocus(context) && Rage(context) >= SpareRage; },
                    HeroicStrike, PickFocusTarget),
                CastRule("sunder armor", P + 30, FightsFocus, SunderArmor, PickFocusTarget),
                CastRule("battle shout", P + 25,
                    [](BotContext const& context) {
                        return FightsFocus(context) && !context.view.HasAura(Self(context), BattleShout);
                    },
                    BattleShout, PickSelf),
            };
            return std::make_shared<Strategy const>(Strategy{"warrior tank", std::move(rules)});
        }
    }
}
