#include "WarriorModule.h"
#include "CommonRules.h"
#include "BotGeometry.h"
#include "ExtraPackWatch.h"
#include "FightSpot.h"
#include "GroupRules.h"
#include "KeepOut.h"
#include "MemberSpot.h"
#include "Ranged.h"
#include "SpellBook.h"
#include "ThreatWatch.h"

#include <algorithm>
#include <map>
#include <set>
#include <utility>
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

            // jvbastel/WOWAI#114: Demoralizing Shout reaches the enemies
            // within DemoralizingShoutYards. The tank shouts when
            // DemoralizingShoutEnemies or more of them lack the debuff, or
            // one in the fight lacks it.
            float const DemoralizingShoutYards = 10.0f;
            std::size_t const DemoralizingShoutEnemies = 2;

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

            // jvbastel/WOWAI#126: the loose enemy that Taunt cannot reach.
            // The rule priorities (P + N), below "taunt an enemy off a
            // member" (54) and above Revenge (40).
            int const PickUpLooseEnemy = 53;
            int const KeepThreatOnTheTaunted = 41;
            // The taunted (or picked-up) enemy is back on the tank when it
            // attacks the tank and the tank's threat on it is at least this
            // ratio of any other member's (BotView::GetThreatRatio). A taunt
            // gives only the top threat (1.0): no lead yet.
            float const KeepThreatLead = 1.1f;
            // The tank goes to a loose enemy at most this far from it: a
            // farther one would lead it through more packs. The walk back
            // still waits for it (a member brings it in).
            float const PickUpYards = 40.0f;

            // jvbastel/WOWAI#168: the rule priorities (P + N) of the
            // abilities on the loose enemy: Taunt, then Sunder Armor or
            // Revenge, both over the pick-up (53).
            int const CastOnTheEnemyItTurnedTo = 57;
            int const TauntTheLooseEnemy = 56;
            int const ThreatOnTheLooseEnemy = 55;

            // jvbastel/WOWAI#172: the early Sunder Armor on each joined
            // enemy. Below the pickup (53 to 57) and Challenging Shout (52),
            // over Demoralizing Shout (50) and the focus rotation.
            int const EarlySunderOnAJoinedEnemy = 51;

            // jvbastel/WOWAI#121: the caster pack. The rule priorities
            // (P + N); the caster wait is CasterComeMs (GroupCoordinator.h).
            int const WarnWhenAPackJoins = 88;
            int const GoInToTheCaster = 72;

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
                Guid const target = PickPullTarget(context);
                if (!CanShoot(context.view))
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

            // jvbastel/WOWAI#173: of `enemies`, the living one that the tank
            // has the biggest threat lead on (BotView::GetThreatRatio). The
            // first of a tie. NoGuid for none.
            Guid MostLedEnemy(BotContext const& context, std::vector<Guid> const& enemies)
            {
                Guid best = NoGuid;
                float bestRatio = 0.0f;
                for (Guid enemy : enemies)
                {
                    if (!IsAlive(context.view, enemy))
                        continue;
                    float const ratio = context.view.GetThreatRatio(Self(context), enemy);
                    if (best == NoGuid || ratio > bestRatio)
                    {
                        best = enemy;
                        bestRatio = ratio;
                    }
                }
                return best;
            }

            // jvbastel/WOWAI#173: the next focus when the focus died. Of the
            // enemies that attack the tank, the one it leads on the most;
            // with none on the tank, of the other enemies in the fight.
            // NoGuid: no enemy is left in the fight.
            Guid PickNextFocus(BotContext const& context)
            {
                Guid const onTank = MostLedEnemy(context, context.view.GetAttackers(Self(context)));
                return onTank != NoGuid ? onTank : MostLedEnemy(context, EnemiesInFight(context));
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
                return UnitsWithin(context.view, LooseEnemies(context), ChallengingShoutYards).size();
            }

            // A Challenging Shout is due: ShoutLooseEnemies or more loose
            // enemies are in its reach.
            bool ChallengingShoutDue(BotContext const& context)
            {
                return FightsFocus(context) && LooseEnemiesInShoutRange(context) >= ShoutLooseEnemies;
            }

            // A due Challenging Shout takes the loose enemies all at once:
            // it goes before Sunder Armor, Revenge, or a walk to one of them.
            bool ShoutTakesThem(BotContext const& context)
            {
                return ChallengingShoutDue(context) && CastableRank(context.view, ChallengingShout, Self(context)) != NoSpell;
            }

            // jvbastel/WOWAI#168: the one loose enemy that the tank takes now,
            // and when it chose it. Its own choice, no group state.
            struct LoosePick
            {
                Guid enemy = NoGuid;
                std::int64_t sinceMs = 0;
                // The last enemy that the tank gave up on after
                // ThreatLossMs: the others come first.
                Guid givenUp = NoGuid;
            };
            using LoosePickPtr = std::shared_ptr<LoosePick>;

            // The rank of loose enemy `enemy` in the pickup order, by the
            // member it attacks: 0 the healer, 1 a member in cloth, 2 any
            // other member.
            int PickupRank(BotContext const& context, Guid enemy)
            {
                Guid const victim = context.view.GetVictim(enemy);
                if (victim != NoGuid && victim == context.group.GetHealer())
                    return 0;
                return context.view.WearsCloth(victim) ? 1 : 2;
            }

            // The tank can act on loose enemy `enemy` from where it is or
            // after a walk: within PickUpYards, and Taunt reaches it now or
            // it stands out of the reach of a pack outside the fight
            // (KeepOut.h, jvbastel/WOWAI#121: the keep-out rule would step
            // the tank out again; the member that the enemy hits steps out
            // itself, and brings it along). The one exception: an add on a
            // healer that cannot step out (TankGoesInForTheHealer): the tank
            // goes in after a warn.
            bool CanTakeLooseEnemy(BotContext const& context, Guid enemy)
            {
                return context.view.GetDistance(enemy) <= PickUpYards
                    && (CastableRank(context.view, Taunt, enemy) != NoSpell
                        || !InOtherPacksReach(context, context.view.GetPosition(enemy))
                        || TankGoesInForTheHealer(context, enemy));
            }

            // The loose enemy that the tank takes now (jvbastel/WOWAI#168).
            // It keeps its choice until that enemy attacks the tank, dies or
            // is no loose enemy any more, or ThreatLossMs pass. Then it
            // chooses again: the lowest PickupRank, the nearest of a rank,
            // and the enemy it gave up on last only when no other is left.
            // NoGuid out of the fight on the focus, or with no loose enemy
            // that the tank can take (CanTakeLooseEnemy).
            Guid PickLooseEnemy(BotContext const& context, LoosePick& pick)
            {
                if (!FightsFocus(context))
                {
                    pick = LoosePick{};
                    return NoGuid;
                }
                std::vector<Guid> const loose = LooseEnemies(context);
                if (pick.enemy != NoGuid)
                {
                    if (!Contains(loose, pick.enemy))
                        pick.enemy = NoGuid;
                    else if (context.nowMs - pick.sinceMs < ThreatLossMs)
                        return pick.enemy;
                    else
                    {
                        pick.givenUp = pick.enemy;
                        pick.enemy = NoGuid;
                    }
                }
                Guid best = NoGuid;
                auto const before = [&](Guid a, Guid b) {
                    if ((a == pick.givenUp) != (b == pick.givenUp))
                        return b == pick.givenUp;
                    int const rankA = PickupRank(context, a), rankB = PickupRank(context, b);
                    if (rankA != rankB)
                        return rankA < rankB;
                    return context.view.GetDistance(a) < context.view.GetDistance(b);
                };
                for (Guid enemy : loose)
                    if (CanTakeLooseEnemy(context, enemy) && (best == NoGuid || before(enemy, best)))
                        best = enemy;
                if (best != NoGuid)
                {
                    pick.enemy = best;
                    pick.sinceMs = context.nowMs;
                }
                return best;
            }

            // jvbastel/WOWAI#126: the tank goes to its loose enemy
            // (PickLooseEnemy) and attacks it when Taunt cannot go out on it
            // now (out of range, on its cooldown, out of Defensive Stance,
            // or not known), and it stands out of the reach of a pack outside
            // the fight, or is an add of TankGoesInForTheHealer. NoGuid when
            // it is not, or when the focus hits another member: the focus
            // comes first (spec #102).
            Guid PickUpTarget(BotContext const& context, LoosePick& pick)
            {
                if (!FightsFocus(context))
                    return NoGuid;
                Guid const focusVictim = context.view.GetVictim(context.group.GetFocusTarget());
                if (focusVictim != NoGuid && focusVictim != Self(context))
                    return NoGuid;
                Guid const loose = PickLooseEnemy(context, pick);
                if (loose != NoGuid && CastableRank(context.view, Taunt, loose) == NoSpell
                    && (!InOtherPacksReach(context, context.view.GetPosition(loose))
                        || TankGoesInForTheHealer(context, loose)))
                    return loose;
                return NoGuid;
            }

            // The tank attacks unit `unit` in melee, and is in melee reach of
            // it or chases it.
            bool MeleesOrChases(BotContext const& context, Guid unit)
            {
                return context.view.GetVictim(Self(context)) == unit && context.view.IsMeleeAttacking()
                    && (context.view.GetDistance(unit) <= MeleeContactYards || context.view.GetChase().target == unit);
            }

            // The tank's visits to its threat target: its own choice, no
            // group state. An ability on the focus ends a visit. A visit
            // belongs to one focus: a new focus (a new fight, after any end
            // of the last one) starts with a new visit.
            struct ThreatVisit
            {
                Guid focus = NoGuid;         // The focus of this visit.
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
                // jvbastel/WOWAI#168: the tank acts on a loose enemy only as
                // its one loose enemy (PickLooseEnemy).
                std::vector<Guid> const loose = LooseEnemies(context);
                for (Guid enemy : EnemiesInFight(context))
                {
                    if (enemy == focus || Contains(loose, enemy)
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
                Guid const focus = context.group.GetFocusTarget();
                std::uint32_t const abilities = visit.focus == focus ? visit.abilities : 0;
                return FightsFocus(context) && abilities < AbilitiesPerVisit
                    && context.view.GetThreatRatio(Self(context), context.group.GetFocusTarget()) >= ThreatTargetLead
                    && PickThreatTarget(context) != NoGuid;
            }

            // The first rank of the ability on the threat target: Revenge
            // when it is ready, else Sunder Armor.
            SpellId ThreatTargetAbility(BotContext const& context, Guid target)
            {
                return CastableRank(context.view, Revenge, target) != NoSpell ? Revenge : SunderArmor;
            }

            // jvbastel/WOWAI#168: the action "Revenge or Sunder Armor
            // (ThreatTargetAbility) on the enemy that `target` picks".
            BotAction ThreatAbilityOn(TargetPicker target)
            {
                return {[target](BotContext const& context) {
                            Guid const enemy = target(context);
                            return CastableRank(context.view, ThreatTargetAbility(context, enemy), enemy) != NoSpell;
                        },
                    [target](BotContext const& context) {
                        Guid const enemy = target(context);
                        context.actions.CastSpell(RankForTarget(context.view, ThreatTargetAbility(context, enemy), enemy), enemy);
                    }};
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

            // A rule of an ability on the unit that `target` picks: on the
            // focus it ends a visit to the threat target, on another enemy
            // it counts as an ability of the visit. Each ability on the
            // focus goes through it, or the visits never end.
            Rule CountsInThreatVisit(Rule rule, TargetPicker target, ThreatVisitPtr const& visit)
            {
                std::function<void(BotContext const&)> const run = rule.action.run;
                rule.action.run = [run, target, visit](BotContext const& context) {
                    Guid const focus = context.group.GetFocusTarget();
                    bool const onFocus = target(context) == focus;
                    run(context);
                    if (visit->focus != focus)
                    {
                        visit->focus = focus;
                        visit->abilities = 0;
                    }
                    if (onFocus)
                        visit->abilities = 0;
                    else
                        ++visit->abilities;
                };
                return rule;
            }

            // jvbastel/WOWAI#126: the loose enemy that the tank last taunted
            // off or picked up, in the fight on one focus. Its own choice,
            // no group state.
            struct ThreatKeep
            {
                Guid focus = NoGuid; // The focus of the fight of the keep.
                Guid enemy = NoGuid;
                // jvbastel/WOWAI#168: the tank cast an ability on the enemy
                // since it turned to it, and when it turned to it.
                bool castOn = false;
                std::int64_t sinceMs = 0;
            };
            using ThreatKeepPtr = std::shared_ptr<ThreatKeep>;

            // jvbastel/WOWAI#168: the action `action` notes that the tank
            // cast an ability on the enemy it keeps threat on.
            BotAction NotesCastOn(BotAction action, ThreatKeepPtr const& keep)
            {
                std::function<void(BotContext const&)> const run = action.run;
                action.run = [run, keep](BotContext const& context) {
                    run(context);
                    keep->castOn = true;
                };
                return action;
            }

            // The enemy that the tank keeps threat on: the one it last taunted
            // off or picked up in this fight, while it lives, is in the fight
            // and not yet back on the tank (KeepThreatLead). Back on the tank
            // too while the tank cast no ability on it yet, for ThreatLossMs
            // (jvbastel/WOWAI#168: no turn to a threat target without a
            // cast). NoGuid else.
            Guid PickKeptEnemy(BotContext const& context, ThreatKeep const& keep)
            {
                Guid const focus = context.group.GetFocusTarget();
                Guid const enemy = keep.enemy;
                if (!FightsFocus(context) || keep.focus != focus || enemy == NoGuid || enemy == focus
                    || !IsAlive(context.view, enemy))
                    return NoGuid;
                Guid const victim = context.view.GetVictim(enemy);
                bool const castDue = !keep.castOn && context.nowMs - keep.sinceMs < ThreatLossMs;
                if (victim == NoGuid
                    || (victim == Self(context) && context.view.GetThreatRatio(Self(context), enemy) >= KeepThreatLead
                        && !castDue))
                    return NoGuid;
                return enemy;
            }

            // jvbastel/WOWAI#168: the tank attacks the enemy that it keeps
            // threat on, and cast no ability on it since it turned to it
            // (PickKeptEnemy): that enemy gets one before the tank turns to
            // another unit. NoGuid else.
            Guid PickEnemyDueACast(BotContext const& context, ThreatKeep const& keep)
            {
                Guid const kept = PickKeptEnemy(context, keep);
                if (kept == NoGuid || keep.castOn || context.view.GetVictim(Self(context)) != kept)
                    return NoGuid;
                return kept;
            }

            // The rule `rule` notes the enemy that `target` picks as the
            // enemy the tank keeps threat on, when it runs. `casts`: the
            // rule casts an ability on it (not a plain attack).
            Rule KeepsThreatOn(Rule rule, TargetPicker target, ThreatKeepPtr const& keep, bool casts = true)
            {
                std::function<void(BotContext const&)> const run = rule.action.run;
                rule.action.run = [run, target, keep, casts](BotContext const& context) {
                    Guid const enemy = target(context);
                    run(context);
                    Guid const focus = context.group.GetFocusTarget();
                    if (keep->enemy != enemy || keep->focus != focus)
                    {
                        keep->castOn = false;
                        keep->sinceMs = context.nowMs;
                    }
                    keep->focus = focus;
                    keep->enemy = enemy;
                    keep->castOn = keep->castOn || casts;
                };
                return rule;
            }

            // A taunt is due and ready: the focus or another enemy hits a
            // member, and the tank can taunt it now. The walk back waits
            // for it, so that no member holds an enemy of the pull.
            bool TauntDue(BotContext const& context, LoosePick& pick)
            {
                SpellId const taunt = HighestKnownRank(context.view, Taunt);
                if (taunt == NoSpell || !FightsFocus(context))
                    return false;
                Guid const focus = PickFocusTarget(context);
                Guid const victim = context.view.GetVictim(focus);
                if (victim != NoGuid && victim != Self(context) && context.view.CanCast(taunt, focus))
                    return true;
                Guid const loose = PickLooseEnemy(context, pick);
                return loose != NoGuid && context.view.CanCast(taunt, loose);
            }

            // Demoralizing Shout is due: DemoralizingShoutEnemies or more
            // enemies within DemoralizingShoutYards lack its debuff, or an
            // enemy in the fight
            // (EnemiesInFight: the focus, or an attacker of a group member)
            // lacks it: a late arrival, or an expired debuff. Not for one
            // enemy out of the fight.
            bool ShoutDue(BotContext const& context)
            {
                if (!FightsFocus(context))
                    return false;
                auto const lacks = [&](Guid enemy) { return !context.view.HasAura(enemy, DemoralizingShout); };
                std::vector<Guid> const inFight = UnitsWithin(context.view, EnemiesInFight(context), DemoralizingShoutYards);
                if (std::any_of(inFight.begin(), inFight.end(), lacks))
                    return true;
                // The other enemies near: those that no member fights yet.
                std::vector<NearEnemy> const near = context.view.GetEnemiesNear(DemoralizingShoutYards);
                return std::count_if(near.begin(), near.end(), [&](NearEnemy const& enemy) { return lacks(enemy.guid); })
                    >= std::ptrdiff_t(DemoralizingShoutEnemies);
            }

            // The focus is a caster that stays away (jvbastel/WOWAI#121):
            // the tank holds the pull back at its fight spot, has not gone
            // in yet, and the focus has mana or casts, is out of melee
            // reach, and does not come.
            bool CasterStaysAway(BotContext const& context)
            {
                PullState const pull = context.group.GetPull();
                if (!HoldsPull(context) || !pull.backAtFightSpot || pull.goesIn || !FightsFocus(context))
                    return false;
                Guid const focus = PickFocusTarget(context);
                bool const caster = context.view.GetMana(focus).max > 0 || context.view.GetCurrentCast(focus) != NoSpell;
                return caster && context.view.GetDistance(focus) > MeleeContactYards && !FocusComes(context);
            }

            // The packs round the tank's pull, for the warn when one joins
            // its fight (jvbastel/WOWAI#121): the tank's own record, no
            // group state. Set when it notes its fight spot.
            struct PullPacks
            {
                Guid target = NoGuid;
                // The pack of each enemy round the pull: 0 for the target's.
                // A pack of the search (SplitIntoPacks, FightSpot.h). A
                // scripted run counts the packs of the route; its judge maps
                // a pack of the search to the route packs of its enemies
                // (FindPullChoices, RunJudge.h).
                std::map<Guid, int> packOf;
                // The packs warned: a pack index, or an enemy of no pack
                // (a roaming mob) by its guid.
                std::set<std::pair<int, Guid>> warned;
            };
            using PullPacksPtr = std::shared_ptr<PullPacks>;

            // The tank is in its own pull of `packs.target`.
            bool InOwnPull(BotContext const& context, PullPacks const& packs)
            {
                PullState const pull = context.group.GetPull();
                return pull.phase != PullPhase::None && pull.puller == Self(context) && pull.target == packs.target;
            }

            // The packs of the enemies in the fight that joined the fight
            // of the pull and have no warn yet. None out of the pull.
            std::set<std::pair<int, Guid>> JoinedPacks(BotContext const& context, PullPacks const& packs)
            {
                std::set<std::pair<int, Guid>> joined;
                if (!InOwnPull(context, packs))
                    return joined;
                for (Guid enemy : EnemiesInFight(context))
                {
                    auto const found = packs.packOf.find(enemy);
                    std::pair<int, Guid> const key =
                        found == packs.packOf.end() ? std::make_pair(-1, enemy) : std::make_pair(found->second, NoGuid);
                    if (key.first != 0 && !packs.warned.count(key))
                        joined.insert(key);
                }
                return joined;
            }

            // The tank warns in party chat that the pull brings `packs`
            // extra packs (0: may bring), once a pull.
            void Warn(BotContext const& context, std::uint32_t packs)
            {
                WarnExtraPacks(context.actions, packs);
                context.group.NoteExtraPackWarn(Self(context));
            }

            // The adds that the tank went in for (TankGoesInForTheHealer):
            // it warns once for each.
            using GoneInPtr = std::shared_ptr<std::set<Guid>>;

            // The pick-up rule `rule` warns when it goes in for an add of a
            // healer that cannot step out: once an add, of the packs it
            // pulls there.
            Rule WarnsWhenItGoesIn(Rule rule, GoneInPtr const& goneIn, LoosePickPtr const& pick)
            {
                std::function<void(BotContext const&)> const run = rule.action.run;
                rule.action.run = [run, goneIn, pick](BotContext const& context) {
                    Guid const enemy = PickUpTarget(context, *pick);
                    if (TankGoesInForTheHealer(context, enemy) && goneIn->insert(enemy).second)
                        Warn(context, PacksPulledAt(context.view, context.view.GetPosition(enemy), FightTarget(context)));
                    run(context);
                };
                return rule;
            }

            // jvbastel/WOWAI#172: the joined enemies that had their early
            // visit, in the pull of `pullTarget`. Its own record, no group
            // state.
            struct EarlyVisits
            {
                Guid pullTarget = NoGuid;
                std::set<Guid> done;
            };
            using EarlyVisitsPtr = std::shared_ptr<EarlyVisits>;

            // An enemy of the fight joined the fight of the tank's pull: it
            // is of no pack of the pull target (PullPacks; a roaming mob too).
            bool JoinedThePull(BotContext const& context, PullPacks const& packs, Guid enemy)
            {
                if (!InOwnPull(context, packs))
                    return false;
                auto const found = packs.packOf.find(enemy);
                return found == packs.packOf.end() || found->second != 0;
            }

            // The joined enemy due its early visit: an enemy of the fight
            // that joined the pull (JoinedThePull), is no loose enemy (the
            // pickup takes those), had no visit and has no Sunder Armor yet,
            // and Revenge or Sunder Armor reaches now. Of several, the lowest
            // threat ratio of the tank, then the nearest. NoGuid when none.
            Guid PickEarlyVisit(BotContext const& context, PullPacks const& packs, EarlyVisits const& visits)
            {
                if (!FightsFocus(context))
                    return NoGuid;
                Guid const focus = context.group.GetFocusTarget();
                bool const samePull = visits.pullTarget == context.group.GetPull().target;
                std::vector<Guid> const loose = LooseEnemies(context);
                Guid best = NoGuid;
                float bestRatio = 0.0f;
                for (Guid enemy : EnemiesInFight(context))
                {
                    if (enemy == focus || Contains(loose, enemy) || !JoinedThePull(context, packs, enemy)
                        || (samePull && visits.done.count(enemy)) || context.view.HasAura(enemy, SunderArmor)
                        || (CastableRank(context.view, Revenge, enemy) == NoSpell
                            && CastableRank(context.view, SunderArmor, enemy) == NoSpell))
                        continue;
                    float const ratio = context.view.GetThreatRatio(Self(context), enemy);
                    if (best == NoGuid || ratio < bestRatio
                        || (ratio == bestRatio && context.view.GetDistance(enemy) < context.view.GetDistance(best)))
                    {
                        best = enemy;
                        bestRatio = ratio;
                    }
                }
                return best;
            }

            // The rule `rule` notes the enemy that `target` picks as visited.
            Rule NotesEarlyVisit(Rule rule, TargetPicker target, EarlyVisitsPtr const& visits)
            {
                std::function<void(BotContext const&)> const run = rule.action.run;
                rule.action.run = [run, target, visits](BotContext const& context) {
                    Guid const enemy = target(context);
                    run(context);
                    Guid const pullTarget = context.group.GetPull().target;
                    if (visits->pullTarget != pullTarget)
                    {
                        visits->pullTarget = pullTarget;
                        visits->done.clear();
                    }
                    visits->done.insert(enemy);
                };
                return rule;
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
            PullPacksPtr const pullPacks = std::make_shared<PullPacks>();
            TargetPicker const revengeTarget = [visit](BotContext const& context) {
                return PickRevengeTarget(context, *visit);
            };
            // The adds of a healer that cannot step out that the tank went
            // in for, each with its warn.
            GoneInPtr const goneIn = std::make_shared<std::set<Guid>>();
            // Each tank keeps threat on its own taunted enemy too
            // (jvbastel/WOWAI#126).
            ThreatKeepPtr const keep = std::make_shared<ThreatKeep>();
            TargetPicker const keptEnemy = [keep](BotContext const& context) { return PickKeptEnemy(context, *keep); };
            // jvbastel/WOWAI#168: and its own loose enemy.
            LoosePickPtr const pick = std::make_shared<LoosePick>();
            TargetPicker const looseEnemy = [pick](BotContext const& context) {
                return PickLooseEnemy(context, *pick);
            };
            TargetPicker const pickUpTarget = [pick](BotContext const& context) { return PickUpTarget(context, *pick); };
            // jvbastel/WOWAI#172: and its own early visits.
            EarlyVisitsPtr const earlyVisits = std::make_shared<EarlyVisits>();
            TargetPicker const earlyVisit = [pullPacks, earlyVisits](BotContext const& context) {
                return PickEarlyVisit(context, *pullPacks, *earlyVisits);
            };
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
                // The focus target dies, or the bot cannot see it any more.
                // jvbastel/WOWAI#173: with enemies left in the fight, the
                // tank sets the next focus (PickNextFocus) and the pull stays
                // on (held), so that a pack that joins later still gets its warn.
                // With none left, the pull and the fight end. A visit to the
                // threat target ends with the focus.
                Bookkeeping("the focus dies", P + 90,
                    [](BotContext const& context) {
                        Guid const focus = context.group.GetFocusTarget();
                        return focus != NoGuid && !IsAlive(context.view, focus);
                    },
                    [visit](BotContext const& context) {
                        Guid const next = PickNextFocus(context);
                        if (next == NoGuid)
                            context.group.EndPull();
                        else
                        {
                            // The pull is held (no change when it is held
                            // already). A pull target that died on its way
                            // leaves a held pull, so the tank does not go on
                            // pulling a dead target.
                            context.group.HoldPull(Self(context), context.nowMs);
                            context.group.SetFocusTarget(next);
                        }
                        visit->abilities = 0;
                    }),
                Bookkeeping("hold the pull", P + 85,
                    [](BotContext const& context) {
                        return IsPulling(context) && context.view.GetVictim(PickPullTarget(context)) == Self(context);
                    },
                    [](BotContext const& context) { context.group.HoldPull(Self(context), context.nowMs); }),

                // Another pack joins the fight of the pull: the tank warns at
                // once, once a pack, whatever brought it (jvbastel/WOWAI#121).
                Bookkeeping("warn when a pack joins", P + WarnWhenAPackJoins,
                    [pullPacks](BotContext const& context) { return !JoinedPacks(context, *pullPacks).empty(); },
                    [pullPacks](BotContext const& context) {
                        std::set<std::pair<int, Guid>> const joined = JoinedPacks(context, *pullPacks);
                        Warn(context, std::uint32_t(joined.size()));
                        pullPacks->warned.insert(joined.begin(), joined.end());
                    }),

                // The pull. The tank picks its fight spot on its cleared way
                // (FightSpot.h), and the pack comes to it there. With no
                // clean spot it warns about the extra packs, once a pull.
                // No Charge: it stuns the target in place.
                Bookkeeping(NoteFightSpotRule, P + 80,
                    [](BotContext const& context) { return IsPulling(context) && !context.group.GetPull().hasFightSpot; },
                    [pullPacks](BotContext const& context) {
                        FightSpot const fight = FindFightSpot(context.view, context.group.GetTrail(), PickPullTarget(context));
                        context.group.SetFightSpot(Self(context), fight.spot);
                        *pullPacks = PullPacks{};
                        pullPacks->target = PickPullTarget(context);
                        for (Guid enemy : fight.targetPack)
                            pullPacks->packOf[enemy] = 0;
                        for (std::size_t pack = 0; pack < fight.otherPacks.size(); ++pack)
                            for (Guid enemy : fight.otherPacks[pack])
                                pullPacks->packOf[enemy] = int(pack) + 1;
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
                // It waits while an enemy hits a member, in Taunt range or not
                // (jvbastel/WOWAI#126): the tank taunts it or goes to it.
                Rule{"walk back to the fight spot", P + 68,
                    [pick](BotContext const& context) {
                        return HoldsPull(context) && context.group.GetPull().hasFightSpot && DistanceToSpot(context) > AtSpotYards
                            && !TauntDue(context, *pick) && LooseEnemies(context).empty();
                    },
                    {[](BotContext const&) { return true; },
                        [opener](BotContext const& context) {
                            if (Rule const* due = FirstDue(opener, context))
                                due->action.run(context);
                            context.actions.KeepMovingTo(context.group.GetPull().fightSpot);
                        }}},

                // A caster that stays away (CasterStaysAway) gets the tank
                // after CasterComeMs: it goes in to the caster's spawn area.
                // When a fight there pulls other packs (KeepOut.h), the tank
                // warns first, unless it warned in this pull already.
                Bookkeeping("go in to the caster's spawn area", P + GoInToTheCaster,
                    [](BotContext const& context) {
                        return CasterStaysAway(context) && context.nowMs - context.group.GetPull().engagedMs >= CasterComeMs;
                    },
                    [](BotContext const& context) {
                        Guid const focus = PickFocusTarget(context);
                        std::uint32_t const packs = PacksPulledAt(context.view, context.view.GetPosition(focus), focus);
                        if (packs > 0 && !context.group.GetPull().extraPackWarned)
                            Warn(context, packs);
                        context.group.GoIn(Self(context));
                    }),

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
                    [keptEnemy, pickUpTarget](BotContext const& context) {
                        Guid const focus = PickFocusTarget(context);
                        if (!FightsFocus(context))
                            return false;
                        // The pull rules move the tank until the pull holds.
                        // A caster that stays away it waits for.
                        if (IsPulling(context) || CasterStaysAway(context))
                            return false;
                        // The tank picks up a loose enemy first, and hits it
                        // until it keeps threat on it (jvbastel/WOWAI#126).
                        Guid const kept = keptEnemy(context);
                        if (pickUpTarget(context) != NoGuid
                            || (kept != NoGuid && context.view.GetVictim(Self(context)) == kept))
                            return false;
                        ChaseState const chase = context.view.GetChase();
                        // The ranged chase of the pull attacks with no melee swing.
                        if (context.view.GetVictim(Self(context)) != focus || !context.view.IsMeleeAttacking())
                            return true;
                        return context.view.GetDistance(focus) > MeleeContactYards && chase.target != focus
                            && !FocusComes(context);
                    },
                    {[](BotContext const& context) { return IsAlive(context.view, PickFocusTarget(context)); },
                        [](BotContext const& context) {
                            Guid const focus = PickFocusTarget(context);
                            context.actions.Attack(focus);
                            if (FocusComes(context) && context.view.GetDistance(focus) > MeleeContactYards)
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
                // jvbastel/WOWAI#168: no turn away from an enemy that the tank
                // attacked with no ability yet (a pick-up): Revenge or Sunder
                // Armor on it first, when it can.
                Rule{"cast on the enemy it turned to", P + CastOnTheEnemyItTurnedTo,
                    [keep](BotContext const& context) { return PickEnemyDueACast(context, *keep) != NoGuid; },
                    NotesCastOn(ThreatAbilityOn([keep](BotContext const& context) { return PickEnemyDueACast(context, *keep); }),
                        keep)},
                // jvbastel/WOWAI#168: the tank's one loose enemy
                // (PickLooseEnemy): Taunt when it can, else Revenge or Sunder
                // Armor. Never Mocking Blow: it needs Battle Stance.
                KeepsThreatOn(CastRule("taunt an enemy off a member", P + TauntTheLooseEnemy,
                                  [looseEnemy](BotContext const& context) { return looseEnemy(context) != NoGuid; },
                                  Taunt, looseEnemy),
                    looseEnemy, keep),
                KeepsThreatOn(
                    Rule{"threat on the loose enemy", P + ThreatOnTheLooseEnemy,
                        [looseEnemy](BotContext const& context) {
                            return looseEnemy(context) != NoGuid && !ShoutTakesThem(context);
                        },
                        ThreatAbilityOn(looseEnemy)},
                    looseEnemy, keep),
                // jvbastel/WOWAI#126: Taunt cannot reach the loose enemy (or
                // waits for its cooldown): the tank goes to it and attacks
                // it. Once in range with Taunt ready, the taunt takes over.
                KeepsThreatOn(WarnsWhenItGoesIn(MeleeRule("pick up a loose enemy", P + PickUpLooseEnemy,
                                                    [pickUpTarget](BotContext const& context) {
                                                        Guid const loose = pickUpTarget(context);
                                                        return loose != NoGuid && !MeleesOrChases(context, loose)
                                                            && !ShoutTakesThem(context);
                                                    },
                                                    pickUpTarget),
                                  goneIn, pick),
                    pickUpTarget, keep, false),
                // jvbastel/WOWAI#126: after the taunt or the pick-up, Revenge
                // or Sunder Armor on that enemy until it is back on the tank,
                // then the focus again. Each counts in a visit to the threat
                // target (jvbastel/WOWAI#117), which waits for the focus.
                CountsInThreatVisit(
                    Rule{"keep threat on the taunted enemy", P + KeepThreatOnTheTaunted,
                        [keptEnemy](BotContext const& context) { return keptEnemy(context) != NoGuid; },
                        NotesCastOn(ThreatAbilityOn(keptEnemy), keep)},
                    keptEnemy, visit),
                // A broken pull: too many loose enemies to taunt one by one.
                CastRule("challenging shout", P + 52,
                    ChallengingShoutDue, ChallengingShout, PickSelf),
                // jvbastel/WOWAI#172: each enemy that joins the fight gets one
                // Revenge or Sunder Armor at once, with no threat lead on the
                // focus (the threat target visits below wait for it).
                NotesEarlyVisit(Rule{"early sunder on a joined enemy", P + EarlySunderOnAJoinedEnemy,
                                    [earlyVisit](BotContext const& context) { return earlyVisit(context) != NoGuid; },
                                    ThreatAbilityOn(earlyVisit)},
                    earlyVisit, earlyVisits),
                // A ready Revenge goes to the threat target on a visit
                // (jvbastel/WOWAI#117), and counts as its ability there. On
                // the focus, it ends the visit.
                CountsInThreatVisit(CastRule("revenge", P + 40, FightsFocus, Revenge, revengeTarget), revengeTarget, visit),
                // Below Revenge: more blocks give more Revenge. The cast
                // check covers the shield and the cooldown.
                CastRule("shield block", P + 38,
                    [](BotContext const& context) {
                        return FightsFocus(context) && context.view.HasAura(Self(context), DefensiveStance)
                            && Rage(context) >= ShieldBlockRage && IsHitInMelee(context, MeleeContactYards);
                    },
                    ShieldBlock, PickSelf),
                // jvbastel/WOWAI#117: one Sunder Armor on the threat target
                // (a ready Revenge goes there through the revenge rule, so
                // this rule only sunders), then the focus again. The focus
                // of the group stays.
                CountsInThreatVisit(CastRule("threat target", P + 37,
                                        [visit](BotContext const& context) { return ThreatVisitDue(context, *visit); },
                                        SunderArmor, PickThreatTarget),
                    PickThreatTarget, visit),
                CountsInThreatVisit(CastRule("heroic strike", P + 35,
                                        [](BotContext const& context) { return FightsFocus(context) && Rage(context) >= SpareRage; },
                                        HeroicStrike, PickFocusTarget),
                    PickFocusTarget, visit),
                CountsInThreatVisit(CastRule("sunder armor", P + 30, FightsFocus, SunderArmor, PickFocusTarget),
                    PickFocusTarget, visit),
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
