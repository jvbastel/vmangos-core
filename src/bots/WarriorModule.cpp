#include "WarriorModule.h"
#include "CommonRules.h"
#include "BotGeometry.h"
#include "GroupRules.h"
#include "Ranged.h"
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
            // The shoot spells reach from 8 to 30 yards (spell range 74).
            // Out of range or sight, the tank walks to ShootStandYards.
            float const MinShootYards = 8.0f;
            float const MaxShootYards = 30.0f;
            float const ShootStandYards = 25.0f;
            // Nearer than this to its pull spot, the tank is back.
            float const SpotYards = 2.0f;
            // Nearer than this, the focus is in melee reach.
            float const MeleeYards = 5.0f;

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

            bool IsCasting(BotContext const& context)
            {
                return context.view.GetCurrentCast(Self(context)) != NoSpell;
            }

            // The tank pulls from range: it wears a ranged weapon with ammo,
            // knows its shoot spell, and the target is not too near for it.
            bool PullsFromRange(BotContext const& context)
            {
                RangedWeapon const weapon = context.view.GetRangedWeapon();
                return CanFire(weapon) && context.view.KnowsSpell(ShootSpell(weapon.kind))
                    && context.view.GetSpellDistance(PickPullTarget(context)) >= MinShootYards;
            }

            // The pull target is out of the range or the sight of a shot.
            bool OutOfShot(BotContext const& context)
            {
                Guid const target = PickPullTarget(context);
                return context.view.GetSpellDistance(target) > MaxShootYards
                    || !context.view.CanSeeFrom(context.view.GetPosition(Self(context)), target);
            }

            // The pulled pack holds on the puller: the tank is the puller of
            // a held pull.
            bool HoldsPull(BotContext const& context)
            {
                PullState const pull = context.group.GetPull();
                return pull.phase == PullPhase::Holding && pull.puller == Self(context);
            }

            float DistanceToSpot(BotContext const& context)
            {
                return Distance2d(context.view.GetPosition(Self(context)), context.group.GetPull().spot);
            }

            // The focus target attacks the tank and runs to it: the tank
            // waits for it where it stands.
            bool FocusComes(BotContext const& context)
            {
                Guid const focus = PickFocusTarget(context);
                return context.view.GetVictim(focus) == Self(context) && context.view.IsMoving(focus);
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

                // The pull. The tank notes where it stands, and the pack
                // comes to it there. No Charge: it stuns the target in place.
                Bookkeeping("note the pull spot", P + 80,
                    [](BotContext const& context) { return IsPulling(context) && !context.group.GetPull().hasSpot; },
                    [](BotContext const& context) {
                        context.group.SetPullSpot(Self(context), context.view.GetPosition(Self(context)));
                    }),
                // From range: a shot with the ranged weapon.
                Rule{"shoot the pull target", P + 75,
                    [](BotContext const& context) {
                        return IsPulling(context) && !IsCasting(context) && PullsFromRange(context) && !OutOfShot(context);
                    },
                    {[](BotContext const& context) {
                         return context.view.CanCast(ShootSpell(context.view.GetRangedWeapon().kind), PickPullTarget(context));
                     },
                        [](BotContext const& context) {
                            context.actions.CastSpell(ShootSpell(context.view.GetRangedWeapon().kind), PickPullTarget(context));
                        }}},
                Rule{"go into shooting range", P + 74,
                    [](BotContext const& context) {
                        return IsPulling(context) && !IsCasting(context) && PullsFromRange(context) && OutOfShot(context);
                    },
                    {[](BotContext const& context) { return IsAlive(context.view, PickPullTarget(context)); },
                        [](BotContext const& context) {
                            context.actions.ChaseToRange(PickPullTarget(context), ShootStandYards);
                        }}},
                // Else the fallback: run in and hit the target once, then
                // walk back ("walk back to the pull spot").
                MeleeRule("run to the pull target", P + 70,
                    [](BotContext const& context) {
                        return IsPulling(context) && !IsCasting(context) && !PullsFromRange(context)
                            && context.view.GetVictim(Self(context)) != PickPullTarget(context);
                    },
                    PickPullTarget),
                Bookkeeping("back at the pull spot", P + 69,
                    [](BotContext const& context) {
                        return HoldsPull(context) && context.group.GetPull().hasSpot && DistanceToSpot(context) <= SpotYards;
                    },
                    [](BotContext const& context) { context.group.ClearPullSpot(Self(context)); }),
                Rule{"walk back to the pull spot", P + 68,
                    [](BotContext const& context) {
                        return HoldsPull(context) && context.group.GetPull().hasSpot && DistanceToSpot(context) > SpotYards;
                    },
                    {[](BotContext const&) { return true; },
                        [](BotContext const& context) { context.actions.KeepMovingTo(context.group.GetPull().spot); }}},

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

                // The fight on the focus target. A focus that runs to the
                // tank finds it where it stands; else the tank runs to it.
                Rule{"attack the focus target", P + 62,
                    [](BotContext const& context) {
                        Guid const focus = PickFocusTarget(context);
                        if (!FightsFocus(context))
                            return false;
                        ChaseState const chase = context.view.GetChase();
                        // A ranged chase from the pull attacks with no melee swing.
                        if (context.view.GetVictim(Self(context)) != focus || (chase.target == focus && chase.ranged))
                            return true;
                        return context.view.GetDistance(focus) > MeleeYards && chase.target != focus
                            && !FocusComes(context);
                    },
                    {[](BotContext const& context) { return IsAlive(context.view, PickFocusTarget(context)); },
                        [](BotContext const& context) {
                            Guid const focus = PickFocusTarget(context);
                            context.actions.Attack(focus);
                            if (FocusComes(context) && context.view.GetDistance(focus) > MeleeYards)
                                context.actions.Stand();
                            else
                                context.actions.Chase(focus);
                        }}},
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
                Rule{"top up the ammo", P + 20,
                    [](BotContext const& context) {
                        return !InCombat(context)
                            && PlanAmmoTopUp(context.view.GetRangedWeapon(), context.view.GetLevel(Self(context))).count > 0;
                    },
                    // Full bags: the rule waits, and lower rules such as
                    // the follow go on.
                    {[](BotContext const& context) {
                         return context.view.HasRoomFor(
                             PlanAmmoTopUp(context.view.GetRangedWeapon(), context.view.GetLevel(Self(context))));
                     },
                        [](BotContext const& context) {
                            context.actions.TopUpAmmo(
                                PlanAmmoTopUp(context.view.GetRangedWeapon(), context.view.GetLevel(Self(context))));
                        }}},
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
