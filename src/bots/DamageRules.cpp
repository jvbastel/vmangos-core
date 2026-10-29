#include "DamageRules.h"
#include "CommonRules.h"
#include "GroupRules.h"
#include "KeepOut.h"
#include "MageModule.h"
#include "SafePlace.h"

#include <algorithm>
#include <vector>

namespace Bots
{
    namespace
    {
        // A focus that hits a member other than the living tank is not the
        // tank's yet: the damage bots wait for the taunt, while the taunt
        // can come. The one exception is the target of a pull: the pull
        // rules hold it. A next focus in a held pull (jvbastel/WOWAI#173)
        // waits for its taunt as any other.
        bool FocusHitsAnotherMember(BotContext const& context, Guid focus)
        {
            PullState const pull = context.group.GetPull();
            if ((pull.phase != PullPhase::None && focus == pull.target) || context.group.IsTauntGivenUp(focus))
                return false;
            Guid const tank = context.group.GetTank();
            Guid const victim = context.view.GetVictim(focus);
            return IsAlive(context.view, tank) && victim != NoGuid && victim != tank
                && victim != context.view.GetSelf();
        }

        // The living enemies in the chase limit that hit a living group
        // member other than the living tank, the bot included, in the
        // order of the group members. It skips the
        // enemy `skip`, and the ones that the bot holds fire on
        // (HoldsFireOn). The tank holds its own attackers.
        std::vector<Guid> LooseEnemies(BotContext const& context, Guid skip)
        {
            Guid const tank = context.group.GetTank();
            bool const tankLives = IsAlive(context.view, tank);
            std::vector<Guid> enemies;
            for (Guid member : context.view.GetGroupMembers())
                if (IsAlive(context.view, member) && !(tankLives && member == tank))
                    for (Guid attacker : context.view.GetAttackers(member))
                        if (attacker != skip && IsAlive(context.view, attacker) && IsInChaseLimit(context, attacker)
                            && !Contains(enemies, attacker) && !HoldsFireOn(context, attacker))
                            enemies.push_back(attacker);
            return enemies;
        }

        // Which other members count in AnotherMemberAttacks.
        enum class Members { All, LowerGuid };

        // True when a group member other than the bot and the player
        // attacks enemy `enemy`; with Members::LowerGuid, only a member whose
        // guid is lower than the bot's counts. The attacks of the player do
        // not count: in a scripted run, the test driver holds no enemy.
        bool AnotherMemberAttacks(BotContext const& context, Guid enemy, Members members = Members::All)
        {
            bool const lowerGuidOnly = members == Members::LowerGuid;
            Guid const self = context.view.GetSelf();
            for (Guid member : context.view.GetGroupMembers())
                if (member != self && member != context.commandPlayer && (!lowerGuidOnly || member < self)
                    && IsAlive(context.view, member) && context.view.GetVictim(member) == enemy)
                    return true;
            return false;
        }

        // The loose enemy of the bot, or NoGuid: the one that the bot
        // attacks, when no member with a lower guid attacks it too; else the
        // first that no member attacks; else the bot's own victim; else the
        // first. So the damage bots spread over the loose enemies, no enemy
        // stays lost, and two bots on one enemy do not swap back and forth.
        Guid PickLooseEnemy(BotContext const& context, Guid skip)
        {
            std::vector<Guid> const enemies = LooseEnemies(context, skip);
            if (enemies.empty())
                return NoGuid;
            Guid const own = context.view.GetVictim(context.view.GetSelf());
            bool const ownIsLoose = Contains(enemies, own);
            if (ownIsLoose && !AnotherMemberAttacks(context, own, Members::LowerGuid))
                return own;
            for (Guid enemy : enemies)
                if (!AnotherMemberAttacks(context, enemy))
                    return enemy;
            return ownIsLoose ? own : enemies.front();
        }
    }

    namespace
    {
        // The living tank, when the bot is not the tank, or NoGuid.
        Guid LivingTank(BotContext const& context)
        {
            Guid const tank = context.group.GetTank();
            return IsAlive(context.view, tank) && tank != context.view.GetSelf() ? tank : NoGuid;
        }

        // The first living enemy that attacks the bot, or NoGuid. Not a
        // frozen one (IsFrozen): it cannot follow the bot to the tank, and
        // the bot steps back from it (StepBack.h, jvbastel/WOWAI#144).
        Guid OwnAttacker(BotContext const& context)
        {
            for (Guid attacker : context.view.GetAttackers(context.view.GetSelf()))
                if (IsAlive(context.view, attacker) && !IsFrozen(context.view, attacker))
                    return attacker;
            return NoGuid;
        }

        // jvbastel/WOWAI#171: the bot is at the living tank, and an enemy
        // in the chase limit attacks it: that enemy, else NoGuid.
        Guid OwnAttackerAtTank(BotContext const& context)
        {
            Guid const tank = LivingTank(context);
            if (tank == NoGuid || context.view.GetDistance(tank) > AtTankYards)
                return NoGuid;
            for (Guid attacker : context.view.GetAttackers(context.view.GetSelf()))
                if (IsAlive(context.view, attacker) && IsInChaseLimit(context, attacker)
                    && !HoldsFireOn(context, attacker))
                    return attacker;
            return NoGuid;
        }

        bool AttackedAwayFromTheTank(BotContext const& context)
        {
            Guid const tank = LivingTank(context);
            return tank != NoGuid && !IsAlive(context.view, context.attackTarget) && OwnAttacker(context) != NoGuid
                && context.view.GetDistance(tank) > AtTankYards;
        }
    }

    bool IsFrozen(BotView const& view, Guid enemy)
    {
        return view.HasAura(enemy, Mage::FrostNova);
    }

    bool HoldsFireOn(BotContext const& context, Guid enemy)
    {
        return IsFrozen(context.view, enemy) && context.view.GetVictim(enemy) != context.group.GetTank();
    }

    namespace
    {
        // PickDamageTarget, with the takeover ratio of the bot's reach for
        // the threat throttle (HoldsBackOnThreat, jvbastel/WOWAI#194).
        Guid PickDamageTargetAt(BotContext const& context, float takeover)
        {
            if (IsAlive(context.view, context.attackTarget))
                return context.attackTarget;
            Guid const healer = PickEnemyHealer(context);
            if (healer != NoGuid && !HoldsFireOn(context, healer) && !HoldsBackOnThreat(context, healer, takeover))
                return healer;
            Guid const focus = context.group.GetFocusTarget();
            bool const waitForTaunt = IsAlive(context.view, focus) && FocusHitsAnotherMember(context, focus);
            // jvbastel/WOWAI#195: the player tank died mid-fight. The focus
            // stays, and the bots fight on against it with no threat wait
            // and no threat throttle (HoldsBackOnThreat holds only with a
            // living tank): no tank holds threat any more.
            if (SeenDead(context.view, PlayerTankOf(context.view, context.group))
                && IsAlive(context.view, focus) && IsInChaseLimit(context, focus) && !HoldsFireOn(context, focus))
                return focus;
            if (!IsAlive(context.view, context.group.GetTank()))
            {
                Guid const loose = PickLooseEnemy(context, waitForTaunt ? focus : NoGuid);
                if (loose != NoGuid)
                    return loose;
            }
            if (IsAlive(context.view, focus) && TankHoldsThreat(context) && !waitForTaunt
                && IsInChaseLimit(context, focus) && !HoldsFireOn(context, focus)
                && !HoldsBackOnThreat(context, focus, takeover))
                return focus;
            return NoGuid;
        }
    }

    Guid PickDamageTarget(BotContext const& context)
    {
        return PickDamageTargetAt(context, RangedTakeoverRatio);
    }

    Guid PickMeleeDamageTarget(BotContext const& context)
    {
        Guid target = PickDamageTargetAt(context, MeleeTakeoverRatio);
        if (target == NoGuid)
            target = OwnAttackerAtTank(context);
        return target != NoGuid && InOtherPacksReach(context, context.view.GetPosition(target)) ? NoGuid : target;
    }

    bool TankHoldsThreat(BotContext const& context)
    {
        PullState const pull = context.group.GetPull();
        if (pull.phase == PullPhase::Holding && !pull.hasContact
            && context.view.GetVictim(pull.target) == pull.puller
            && context.view.GetDistanceBetween(pull.target, pull.puller) <= MeleeContactYards)
            context.group.NoteFocusContact(context.nowMs);
        return context.group.TankHoldsThreat(context.nowMs);
    }

    bool HasDamageTarget(BotContext const& context)
    {
        return PickDamageTarget(context) != NoGuid;
    }

    Rule CloseInRule(int priority)
    {
        Rule rule;
        rule.name = "run into casting range";
        rule.priority = priority;
        rule.trigger = [](BotContext const& context) {
            return HasDamageTarget(context) && context.view.GetSpellDistance(PickDamageTarget(context)) > CastingRangeYards;
        };
        rule.action.canDo = [](BotContext const&) { return true; };
        rule.action.run = [](BotContext const& context) {
            context.actions.ChaseToRange(PickDamageTarget(context), RangedStandYards);
        };
        return rule;
    }

    namespace
    {
        // jvbastel/WOWAI#160: the damage target of a ranged bot that casts
        // nothing, in casting range and out of its sight, else NoGuid.
        Guid TargetOutOfSight(BotContext const& context)
        {
            BotView const& view = context.view;
            Guid const self = view.GetSelf();
            Guid const target = PickDamageTarget(context);
            if (target == NoGuid || view.GetCurrentCast(self) != NoSpell
                || view.GetSpellDistance(target) > CastingRangeYards
                || view.CanSeeFrom(view.GetPosition(self), target))
                return NoGuid;
            return target;
        }
    }

    Rule TargetSightRule(int priority)
    {
        Rule rule;
        rule.name = TargetSightRuleName;
        rule.priority = priority;
        rule.trigger = [](BotContext const& context) { return TargetOutOfSight(context) != NoGuid; };
        rule.action.canDo = [](BotContext const&) { return true; };
        rule.action.run = [](BotContext const& context) {
            BotView const& view = context.view;
            Guid const target = TargetOutOfSight(context);
            std::vector<NearEnemy> const keepOutOf =
                EnemiesOutsideTheFight(view, view.GetPosition(target), FightTarget(context));
            Position spot;
            if (FindSafeCastPlace(view, target, RangedStandYards, spot, nullptr, nullptr, &keepOutOf))
                context.actions.KeepMovingTo(spot);
            else
                context.actions.ChaseToRange(target, RangedStandYards);
        };
        return rule;
    }

    Rule WalkToTankRule(int priority)
    {
        Rule rule;
        rule.name = "walk to the tank when attacked";
        rule.priority = priority;
        rule.trigger = AttackedAwayFromTheTank;
        rule.action.canDo = [](BotContext const&) { return true; };
        // Once the bot follows the tank, it keeps walking: a new follow
        // each think would start the move again.
        rule.action.run = [](BotContext const& context) {
            Guid const tank = context.group.GetTank();
            if (context.view.GetFollowTarget() != tank)
                context.actions.Follow(tank);
        };
        return rule;
    }

    bool HoldsBackOnThreat(BotContext const& context, Guid enemy, float takeover)
    {
        Guid const self = context.view.GetSelf();
        if (!IsAlive(context.view, PlayerTankOf(context.view, context.group)))
            return false;
        float const ratio = context.view.GetThreatRatio(self, enemy);
        bool const holds = context.group.HoldsBackOnThreat(self, enemy);
        if (holds && ratio < ThreatStartShare * takeover)
        {
            context.group.EndThreatHold(self, enemy);
            return false;
        }
        if (!holds && ratio >= ThreatStopShare * takeover)
        {
            // The holds on dead enemies go first, so that they never pile up.
            for (Guid held : context.group.GetThreatHolds(self))
                if (!IsAlive(context.view, held))
                    context.group.EndThreatHold(self, held);
            context.group.HoldBackOnThreat(self, enemy);
            return true;
        }
        return holds;
    }
}
