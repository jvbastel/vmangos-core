#include "WarriorModule.h"
#include "CommonRules.h"
#include "BotGeometry.h"
#include "ExtraPackWatch.h"
#include "FightSpot.h"
#include "GroupRules.h"
#include "MemberSpot.h"
#include "Ranged.h"
#include "SpellBook.h"

#include <algorithm>
#include <vector>

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
            // Out of shooting range (Ranged.h), the tank walks to this.
            float const ShootStandYards = 25.0f;
            // A chase into shooting range counts as ended only after this
            // many thinks in a row with no move: right after the order (or
            // a new path) the tank does not move yet.
            std::uint32_t const StillChaseThinks = 3;
            // Nearer than this, the focus is in melee reach.
            float const MeleeYards = 5.0f;

            // jvbastel/WOWAI#114: Demoralizing Shout reaches the enemies
            // within ShoutYards. The tank shouts when ShoutEnemies or more
            // of them lack the debuff, or one in the fight lacks it.
            float const ShoutYards = 10.0f;
            std::size_t const ShoutEnemies = 2;

            // jvbastel/WOWAI#116: Shield Block.
            // Rage for Shield Block from it.
            std::uint32_t const ShieldBlockRage = 10;

            // jvbastel/WOWAI#115: Challenging Shout from this many loose
            // enemies.
            std::size_t const ShoutLooseEnemies = 3;
            // The radius of Challenging Shout: it counts only the loose
            // enemies this near.
            float const ChallengingShoutYards = 10.0f;

            // jvbastel/WOWAI#117: the threat target. From this threat lead
            // on the focus (BotView::GetThreatRatio; 1.5 is 150%), the tank
            // turns to its threat target, for this many abilities a visit.
            float const ThreatTargetLead = 1.5f;
            std::uint32_t const AbilitiesPerVisit = 1;

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

            // The ammo the tank tops up now for its ranged weapon and level.
            AmmoTopUp AmmoPlan(BotContext const& context)
            {
                return PlanAmmoTopUp(context.view.GetRangedWeapon(), context.view.GetLevel(Self(context)));
            }

            // The pull target is out of the range or the sight of a shot
            // (InShotFrom, Ranged.h): too far, too near, or out of sight.
            bool OutOfShot(BotContext const& context)
            {
                return !InShotFrom(context.view, context.view.GetPosition(Self(context)), PickPullTarget(context));
            }

            // The tank pulls from range: it wears a ranged weapon with ammo,
            // knows its shoot spell, and the target is not too near for it,
            // or the fight spot search found it a shoot spot (FightSpot.h).
            // Not when its chase into range ended out of shot (the path
            // cannot get nearer; StillChaseThinks), nor once it ran in
            // to hit (a melee attack of the target): then the pull is the
            // fallback.
            bool PullsFromRange(BotContext const& context)
            {
                RangedWeapon const weapon = context.view.GetRangedWeapon();
                Guid const target = PickPullTarget(context);
                if (!CanFire(weapon) || !context.view.KnowsSpell(ShootSpell(weapon.kind)))
                    return false;
                if (ShotReachAt(context.view.GetSpellDistance(target)) == ShotReach::TooNear
                    && !context.group.GetPull().hasShootSpot)
                    return false;
                if (context.view.GetVictim(Self(context)) == target && context.view.IsMeleeAttacking())
                    return false;
                return !(context.view.GetChase().target == target && !context.view.IsMoving(Self(context))
                    && context.group.GetPull().stillChaseThinks >= StillChaseThinks && OutOfShot(context));
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
                return Distance2d(context.view.GetPosition(Self(context)), context.group.GetPull().fightSpot);
            }

            float DistanceToShootSpot(BotContext const& context)
            {
                return Distance2d(context.view.GetPosition(Self(context)), context.group.GetPull().shootSpot);
            }

            // The tank pulls from range from its shoot spot, and stands
            // out of shot.
            bool GoesToShoot(BotContext const& context)
            {
                return IsPulling(context) && context.group.GetPull().hasShootSpot && !IsCasting(context)
                    && PullsFromRange(context) && OutOfShot(context);
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

            // The living enemies that hit a living group member other than the
            // tank and are not the focus target: the loose enemies. The
            // enemies on the healer come first (jvbastel/WOWAI#115), then the
            // rest in the order of the group members.
            std::vector<Guid> LooseEnemies(BotContext const& context)
            {
                return MemberAttackers(context, Self(context), context.group.GetFocusTarget(), context.group.GetHealer());
            }

            // The loose enemies that a Challenging Shout reaches.
            std::size_t LooseEnemiesInShoutRange(BotContext const& context)
            {
                std::vector<Guid> const loose = LooseEnemies(context);
                return std::count_if(loose.begin(), loose.end(),
                    [&](Guid enemy) { return context.view.GetDistance(enemy) <= ChallengingShoutYards; });
            }

            // The loose enemy that the tank taunts off: one on the healer
            // first.
            Guid PickLooseEnemy(BotContext const& context)
            {
                std::vector<Guid> const loose = LooseEnemies(context);
                return loose.empty() ? NoGuid : loose.front();
            }

            // The tank's visits to its threat target: its own choice, no
            // group state. An ability on the focus ends a visit.
            struct ThreatVisit
            {
                std::uint32_t abilities = 0; // On the threat target, this visit.
            };
            using ThreatVisitPtr = std::shared_ptr<ThreatVisit>;

            // The threat target: of the enemies in the fight other than the
            // focus that the tank can hit now with Revenge or Sunder Armor,
            // the one with the lowest threat ratio of the tank. A tie: the
            // one nearest the healer, if it lives. NoGuid when there is none.
            Guid PickThreatTarget(BotContext const& context)
            {
                Guid const focus = context.group.GetFocusTarget();
                // The tie-break needs a living healer in the group: the
                // coordinator does not clear a healer that died or left.
                Guid healer = context.group.GetHealer();
                if (!IsAlive(context.view, healer) || !IsGroupMember(context.view, healer))
                    healer = NoGuid;
                Guid best = NoGuid;
                float bestRatio = 0.0f;
                float bestYards = 0.0f;
                for (Guid enemy : EnemiesInFight(context))
                {
                    if (enemy == focus
                        || (CastableRank(context.view, Revenge, enemy) == NoSpell
                            && CastableRank(context.view, SunderArmor, enemy) == NoSpell))
                        continue;
                    float const ratio = context.view.GetThreatRatio(Self(context), enemy);
                    float const yards = healer == NoGuid ? 0.0f : context.view.GetDistanceBetween(enemy, healer);
                    if (best == NoGuid || ratio < bestRatio || (ratio == bestRatio && yards < bestYards))
                    {
                        best = enemy;
                        bestRatio = ratio;
                        bestYards = yards;
                    }
                }
                return best;
            }

            // A visit to the threat target is due: the lead on the focus
            // holds, the visit has abilities left, and a threat target is
            // in reach.
            bool ThreatVisitDue(BotContext const& context, ThreatVisit const& visit)
            {
                return FightsFocus(context) && visit.abilities < AbilitiesPerVisit
                    && context.view.GetThreatRatio(Self(context), context.group.GetFocusTarget()) >= ThreatTargetLead
                    && PickThreatTarget(context) != NoGuid;
            }

            // The first rank of the ability on the threat target: Revenge
            // when it is ready, else Sunder Armor.
            SpellId ThreatTargetAbility(BotContext const& context, Guid target)
            {
                return CastableRank(context.view, Revenge, target) != NoSpell ? Revenge : SunderArmor;
            }

            // Revenge goes to the threat target when a visit is due and
            // Revenge is its ability, else to the focus. So Revenge keeps
            // its place above Shield Block on a visit too.
            Guid PickRevengeTarget(BotContext const& context, ThreatVisit const& visit)
            {
                if (ThreatVisitDue(context, visit))
                {
                    Guid const target = PickThreatTarget(context);
                    if (ThreatTargetAbility(context, target) == Revenge)
                        return target;
                }
                return PickFocusTarget(context);
            }

            // A rule of an ability on the focus: it ends a visit to the
            // threat target.
            Rule EndsThreatVisit(Rule rule, ThreatVisitPtr const& visit)
            {
                std::function<void(BotContext const&)> const run = rule.action.run;
                rule.action.run = [run, visit](BotContext const& context) {
                    run(context);
                    visit->abilities = 0;
                };
                return rule;
            }

            // A living enemy hits the tank in melee reach: a hit that a
            // shield can block.
            bool HitInMelee(BotContext const& context)
            {
                for (Guid const attacker : context.view.GetAttackers(Self(context)))
                    if (IsAlive(context.view, attacker) && context.view.GetDistance(attacker) <= MeleeYards)
                        return true;
                return false;
            }

            // A taunt is due and ready: the focus or another enemy hits a
            // member, and the tank can taunt it now. The walk back waits
            // for it, so that no member holds an enemy of the pull.
            bool TauntDue(BotContext const& context)
            {
                SpellId const taunt = HighestKnownRank(context.view, Taunt);
                if (taunt == NoSpell || !FightsFocus(context))
                    return false;
                Guid const focus = PickFocusTarget(context);
                Guid const victim = context.view.GetVictim(focus);
                if (victim != NoGuid && victim != Self(context) && context.view.CanCast(taunt, focus))
                    return true;
                Guid const loose = PickLooseEnemy(context);
                return loose != NoGuid && context.view.CanCast(taunt, loose);
            }

            // Demoralizing Shout is due: ShoutEnemies or more enemies within
            // ShoutYards lack its debuff, or an enemy in the fight
            // (EnemiesInFight: the focus, or an attacker of a group member)
            // lacks it: a late arrival, or an expired debuff. Not for one
            // enemy out of the fight.
            bool ShoutDue(BotContext const& context)
            {
                if (!FightsFocus(context))
                    return false;
                std::vector<Guid> const inFight = EnemiesInFight(context);
                std::size_t lacking = 0;
                for (NearEnemy const& enemy : context.view.GetEnemiesNear(ShoutYards))
                {
                    if (context.view.HasAura(enemy.guid, DemoralizingShout))
                        continue;
                    if (std::find(inFight.begin(), inFight.end(), enemy.guid) != inFight.end())
                        return true;
                    ++lacking;
                }
                return lacking >= ShoutEnemies;
            }

            // The tank warns in party chat that the pull brings `packs`
            // extra packs (0: may bring), once a pull.
            void Warn(BotContext const& context, std::uint32_t packs)
            {
                WarnExtraPacks(context.actions, packs);
                context.group.NoteExtraPackWarn(Self(context));
            }

            // The first rule of `rules` whose trigger fires and which the
            // tank can do now, or null.
            Rule const* FirstDue(std::vector<Rule> const& rules, BotContext const& context)
            {
                for (Rule const& rule : rules)
                    if (rule.trigger(context) && rule.action.canDo(context))
                        return &rule;
                return nullptr;
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
            // Each strategy, so each tank, has its own visits.
            ThreatVisitPtr const visit = std::make_shared<ThreatVisit>();
            // The opener, highest first: Defensive Stance, then a due
            // Demoralizing Shout (jvbastel/WOWAI#114: threat on each enemy
            // near), and Bloodrage with little rage, which gives the rage.
            // Each is instant, so it also fires on the walk back.
            std::vector<Rule> const opener = {
                CastRule("defensive stance", P + 60,
                    [](BotContext const& context) {
                        return FightsFocus(context) && !context.view.HasAura(Self(context), DefensiveStance);
                    },
                    DefensiveStance, PickSelf),
                CastRule("demoralizing shout", P + 50, ShoutDue, DemoralizingShout, PickSelf),
                CastRule("bloodrage", P + 45,
                    [](BotContext const& context) { return FightsFocus(context) && Rage(context) < LowRage; },
                    Bloodrage, PickSelf),
            };
            std::vector<Rule> rules = {
                // The pull and the fight end when the focus target dies or
                // the bot cannot see it any more. A visit to the threat
                // target ends with them.
                Bookkeeping("end the fight", P + 90,
                    [](BotContext const& context) {
                        Guid const focus = context.group.GetFocusTarget();
                        return focus != NoGuid && !IsAlive(context.view, focus);
                    },
                    [visit](BotContext const& context) {
                        context.group.EndPull();
                        visit->abilities = 0;
                    }),
                Bookkeeping("hold the pull", P + 85,
                    [](BotContext const& context) {
                        return IsPulling(context) && context.view.GetVictim(PickPullTarget(context)) == Self(context);
                    },
                    [](BotContext const& context) { context.group.HoldPull(Self(context), context.nowMs); }),

                // The pull. The tank picks its fight spot on its cleared way
                // (FightSpot.h), and the pack comes to it there. With no
                // clean spot it warns about the extra packs, once a pull.
                // No Charge: it stuns the target in place.
                Bookkeeping(NoteFightSpotRule, P + 80,
                    [](BotContext const& context) { return IsPulling(context) && !context.group.GetPull().hasFightSpot; },
                    [](BotContext const& context) {
                        FightSpot const fight = FindFightSpot(context.view, context.group.GetTrail(), PickPullTarget(context));
                        context.group.SetFightSpot(Self(context), fight.spot);
                        context.group.StartGroupWait(Self(context), context.nowMs);
                        if (fight.hasShootSpot)
                            context.group.SetShootSpot(Self(context), fight.shootSpot);
                        if (!fight.clean)
                            Warn(context, fight.extraPacks);
                    }),
                // The group goes to its member spots (MemberSpot.h): the tank
                // pulls when each bot is in place, or after GroupWaitMs. It
                // may walk to its shoot spot meanwhile. A wait of
                // GroupWaitSayMs it says in party chat, once a pull.
                Rule{WaitForTheGroupRule, P + 76, TankWaitsForTheGroup,
                    {[](BotContext const&) { return true; },
                        [](BotContext const& context) {
                            PullState const pull = context.group.GetPull();
                            if (pull.waitSaid != GroupWaitSaid::Nothing || context.nowMs - pull.waitFromMs < GroupWaitSayMs)
                                return;
                            context.actions.SayParty(GroupWaitLine(std::uint32_t(MembersNotInPlace(context).size())), PartyLine::Plain);
                            context.group.NoteGroupWaitSaid(Self(context), GroupWaitSaid::Waits);
                        }}},
                // The wait ended with bots not in place: the tank says that
                // it pulls without them, once a pull.
                Bookkeeping("pull without the group", P + 79,
                    [](BotContext const& context) {
                        PullState const pull = context.group.GetPull();
                        return IsPulling(context) && pull.hasFightSpot && pull.waitSaid != GroupWaitSaid::PullsWithout
                            && context.nowMs - pull.waitFromMs >= GroupWaitMs && !MembersNotInPlace(context).empty();
                    },
                    [](BotContext const& context) {
                        context.actions.SayParty(PullWithoutLine(std::uint32_t(MembersNotInPlace(context).size())), PartyLine::Plain);
                        context.group.NoteGroupWaitSaid(Self(context), GroupWaitSaid::PullsWithout);
                    }),
                // The tank cannot shoot from where it stands: it walks to
                // its shoot spot. No shot from there either (the sight of
                // the game): it pulls as without one.
                Bookkeeping("no shot from the shoot spot", P + 78,
                    [](BotContext const& context) {
                        return GoesToShoot(context) && DistanceToShootSpot(context) <= AtSpotYards;
                    },
                    [](BotContext const& context) {
                        // Its chase to the target from here is no checked
                        // way (FightSpot.h): the tank warns, once a pull.
                        context.group.ClearShootSpot(Self(context));
                        if (!context.group.GetPull().extraPackWarned)
                            Warn(context, 0);
                    }),
                Rule{"go to the shoot spot", P + 77,
                    [](BotContext const& context) {
                        return GoesToShoot(context) && DistanceToShootSpot(context) > AtSpotYards;
                    },
                    {[](BotContext const&) { return true; },
                        [](BotContext const& context) { context.actions.KeepMovingTo(context.group.GetPull().shootSpot); }}},
                // From range: a shot with the ranged weapon.
                Rule{"shoot the pull target", P + 75,
                    [](BotContext const& context) {
                        return IsPulling(context) && !IsCasting(context) && PullsFromRange(context) && !OutOfShot(context);
                    },
                    {[](BotContext const& context) {
                         return context.view.CanCast(ShootSpell(context.view.GetRangedWeapon().kind), PickPullTarget(context));
                     },
                        [](BotContext const& context) {
                            // No chase may move the tank out of its cast.
                            context.actions.Stand();
                            context.actions.CastSpell(ShootSpell(context.view.GetRangedWeapon().kind), PickPullTarget(context));
                        }}},
                Rule{"go into shooting range", P + 74,
                    [](BotContext const& context) {
                        return IsPulling(context) && !IsCasting(context) && PullsFromRange(context) && OutOfShot(context);
                    },
                    {[](BotContext const& context) { return IsAlive(context.view, PickPullTarget(context)); },
                        [](BotContext const& context) {
                            // Too far, or in range but out of sight: a ranged
                            // chase. The core ends its path at the first point
                            // within ShootStandYards that the target sees (on
                            // a ledge that can be a point with no shot; then
                            // FindShotOnPath gives the shoot spot). A melee
                            // chase would not do: the core moves a
                            // chase only toward the bot's victim, and the
                            // ranged chase makes the target the victim with
                            // no melee swing (jvbastel/WOWAI#122).
                            Guid const target = PickPullTarget(context);
                            context.group.CountRangeChase(Self(context), context.view.IsMoving(Self(context)));
                            context.actions.ChaseToRange(target, ShootStandYards);
                        }}},
                // Else the fallback: run in and hit the target once, then
                // walk back ("walk back to the fight spot").
                MeleeRule("run to the pull target", P + 70,
                    [](BotContext const& context) {
                        // A ranged chase made the target the victim, with no
                        // melee swing: that is no hit yet. A melee victim out
                        // of combat with no chase is none either: a hit that
                        // started no fight (run 12 of jvbastel/WOWAI#103,
                        // pack4), where the tank would wait for good.
                        Guid const target = PickPullTarget(context);
                        return IsPulling(context) && !IsCasting(context) && !PullsFromRange(context)
                            && (context.view.GetVictim(Self(context)) != target || !context.view.IsMeleeAttacking()
                                || (!InCombat(context) && context.view.GetChase().target != target));
                    },
                    PickPullTarget),
                Bookkeeping("back at the fight spot", P + 69,
                    [](BotContext const& context) {
                        return HoldsPull(context) && context.group.GetPull().hasFightSpot && DistanceToSpot(context) <= AtSpotYards;
                    },
                    [](BotContext const& context) { context.group.ClearFightSpot(Self(context)); }),
                // A due opener ability (after a ranged pull, spec story 3)
                // fires on the way, and the walk goes on in the same think.
                Rule{"walk back to the fight spot", P + 68,
                    [](BotContext const& context) {
                        return HoldsPull(context) && context.group.GetPull().hasFightSpot && DistanceToSpot(context) > AtSpotYards
                            && !TauntDue(context);
                    },
                    {[](BotContext const&) { return true; },
                        [opener](BotContext const& context) {
                            if (Rule const* due = FirstDue(opener, context))
                                due->action.run(context);
                            context.actions.KeepMovingTo(context.group.GetPull().fightSpot);
                        }}},

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
                        // The pull rules move the tank until the pull holds.
                        if (IsPulling(context))
                            return false;
                        ChaseState const chase = context.view.GetChase();
                        // The ranged chase of the pull attacks with no melee swing.
                        if (context.view.GetVictim(Self(context)) != focus || !context.view.IsMeleeAttacking())
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
                // A broken pull: too many loose enemies to taunt one by one.
                CastRule("challenging shout", P + 52,
                    [](BotContext const& context) {
                        return FightsFocus(context) && LooseEnemiesInShoutRange(context) >= ShoutLooseEnemies;
                    },
                    ChallengingShout, PickSelf),
                // A ready Revenge goes to the threat target on a visit
                // (jvbastel/WOWAI#117), and counts as its ability there. On
                // the focus, it ends the visit.
                Rule{"revenge", P + 40, FightsFocus,
                    {[visit](BotContext const& context) {
                         Guid const target = PickRevengeTarget(context, *visit);
                         return target != NoGuid && CastableRank(context.view, Revenge, target) != NoSpell;
                     },
                        [visit](BotContext const& context) {
                            Guid const target = PickRevengeTarget(context, *visit);
                            context.actions.CastSpell(RankForTarget(context.view, Revenge, target), target);
                            if (target == context.group.GetFocusTarget())
                                visit->abilities = 0;
                            else
                                ++visit->abilities;
                        }}},
                // Below Revenge: more blocks give more Revenge. The cast
                // check covers the shield and the cooldown.
                CastRule("shield block", P + 38,
                    [](BotContext const& context) {
                        return FightsFocus(context) && context.view.HasAura(Self(context), DefensiveStance)
                            && Rage(context) >= ShieldBlockRage && HitInMelee(context);
                    },
                    ShieldBlock, PickSelf),
                // jvbastel/WOWAI#117: one Sunder Armor on the threat target
                // (a ready Revenge goes there through the revenge rule), then
                // the focus again. The focus of the group stays.
                Rule{"threat target", P + 37,
                    [visit](BotContext const& context) { return ThreatVisitDue(context, *visit); },
                    {[](BotContext const& context) {
                         Guid const target = PickThreatTarget(context);
                         return target != NoGuid
                             && CastableRank(context.view, ThreatTargetAbility(context, target), target) != NoSpell;
                     },
                        [visit](BotContext const& context) {
                            Guid const target = PickThreatTarget(context);
                            context.actions.CastSpell(
                                RankForTarget(context.view, ThreatTargetAbility(context, target), target), target);
                            ++visit->abilities;
                        }}},
                EndsThreatVisit(CastRule("heroic strike", P + 35,
                                    [](BotContext const& context) { return FightsFocus(context) && Rage(context) >= SpareRage; },
                                    HeroicStrike, PickFocusTarget),
                    visit),
                EndsThreatVisit(CastRule("sunder armor", P + 30, FightsFocus, SunderArmor, PickFocusTarget), visit),
                Rule{"top up the ammo", P + 20,
                    [](BotContext const& context) {
                        return !InCombat(context)
                            && AmmoPlan(context).count > 0;
                    },
                    // Full bags: the rule waits, and lower rules such as
                    // the follow go on.
                    {[](BotContext const& context) {
                         return context.view.HasRoomFor(
                             AmmoPlan(context));
                     },
                        [](BotContext const& context) {
                            context.actions.TopUpAmmo(
                                AmmoPlan(context));
                        }}},
                CastRule("battle shout", P + 25,
                    [](BotContext const& context) {
                        return FightsFocus(context) && !context.view.HasAura(Self(context), BattleShout);
                    },
                    BattleShout, PickSelf),
            };
            rules.insert(rules.end(), opener.begin(), opener.end());
            return std::make_shared<Strategy const>(Strategy{"warrior tank", std::move(rules)});
        }
    }
}
