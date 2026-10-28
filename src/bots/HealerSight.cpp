#include "HealerSight.h"
#include "CommonRules.h"
#include "GroupCoordinator.h"
#include "KeepOut.h"
#include "PriestModule.h"
#include "SafePlace.h"
#include "SpellBook.h"

#include <vector>

namespace Bots
{
    namespace
    {
        // The living tank of a player-tank group in a fight of the bot,
        // when the bot is not the tank and casts nothing, else NoGuid.
        Guid TankToKeep(BotContext const& context)
        {
            Guid const self = context.view.GetSelf();
            Guid const tank = context.group.GetTank();
            if (!context.group.IsPlayerTankGroup() || tank == self || !IsAlive(context.view, tank)
                || !context.view.IsInCombat(self) || context.view.GetCurrentCast(self) != NoSpell)
                return NoGuid;
            return tank;
        }

        bool SeesInReach(BotView const& view, Guid tank)
        {
            return view.CanSeeFrom(view.GetPosition(view.GetSelf()), tank)
                && view.GetSpellDistance(tank) <= Priest::HealReachYards;
        }
    }

    bool FindTankSightSpot(BotContext const& context, Position& spot)
    {
        BotView const& view = context.view;
        Guid const tank = TankToKeep(context);
        if (tank == NoGuid || SeesInReach(view, tank))
            return false;

        std::vector<NearEnemy> const keepOutOf =
            EnemiesOutsideTheFight(view, view.GetPosition(tank), FightTarget(context));
        return FindSafeCastPlace(view, tank, TankSightMostYards, spot, nullptr,
            [&view, tank](Position const& at) { return view.GetSpellDistanceFrom(at, tank) <= TankSightMostYards; },
            &keepOutOf);
    }

    Rule TankSightRule(int priority)
    {
        Rule rule;
        rule.name = TankSightRuleName;
        rule.priority = priority;
        rule.trigger = [](BotContext const& context) {
            Guid const tank = TankToKeep(context);
            return tank != NoGuid && !SeesInReach(context.view, tank);
        };
        rule.action.canDo = [](BotContext const& context) {
            Position spot;
            return FindTankSightSpot(context, spot);
        };
        rule.action.run = [](BotContext const& context) {
            Position spot;
            if (FindTankSightSpot(context, spot))
                context.actions.KeepMovingTo(spot);
        };
        return rule;
    }

    bool ResurrectsTankBeforeRest(BotView const& view, GroupCoordinator const& group)
    {
        Guid const self = view.GetSelf();
        Guid const tank = group.GetTank();
        if (!group.IsPlayerTankGroup() || tank == self || !IsAlive(view, self) || IsGroupInCombat(view)
            || view.GetLifeState(tank) != LifeState::Dead || group.IsResurrectGivenUp(tank))
            return false;
        SpellId const rank = RankForTarget(view, Priest::Resurrection, tank);
        return rank != NoSpell && view.GetRankAmounts(rank).manaCost <= view.GetMana(self).current;
    }
}
