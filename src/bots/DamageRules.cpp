#include "DamageRules.h"
#include "CommonRules.h"
#include "GroupRules.h"
#include "KeepOut.h"

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
        // enemy `skip`. The tank holds its own attackers.
        std::vector<Guid> LooseEnemies(BotContext const& context, Guid skip)
        {
            Guid const tank = context.group.GetTank();
            bool const tankLives = IsAlive(context.view, tank);
            std::vector<Guid> enemies;
            for (Guid member : context.view.GetGroupMembers())
                if (IsAlive(context.view, member) && !(tankLives && member == tank))
                    for (Guid attacker : context.view.GetAttackers(member))
                        if (attacker != skip && IsAlive(context.view, attacker) && IsInChaseLimit(context, attacker)
                            && !Contains(enemies, attacker))
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

        // The first living enemy that attacks the bot, or NoGuid.
        Guid OwnAttacker(BotContext const& context)
        {
            for (Guid attacker : context.view.GetAttackers(context.view.GetSelf()))
                if (IsAlive(context.view, attacker))
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
                if (IsAlive(context.view, attacker) && IsInChaseLimit(context, attacker))
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

    Guid PickDamageTarget(BotContext const& context)
    {
        if (IsAlive(context.view, context.attackTarget))
            return context.attackTarget;
        Guid const healer = PickEnemyHealer(context);
        if (healer != NoGuid)
            return healer;
        Guid const focus = context.group.GetFocusTarget();
        bool const waitForTaunt = IsAlive(context.view, focus) && FocusHitsAnotherMember(context, focus);
        if (!IsAlive(context.view, context.group.GetTank()))
        {
            Guid const loose = PickLooseEnemy(context, waitForTaunt ? focus : NoGuid);
            if (loose != NoGuid)
                return loose;
        }
        if (IsAlive(context.view, focus) && TankHoldsThreat(context) && !waitForTaunt
            && IsInChaseLimit(context, focus))
            return focus;
        return NoGuid;
    }

    Guid PickMeleeDamageTarget(BotContext const& context)
    {
        Guid target = PickDamageTarget(context);
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
}
