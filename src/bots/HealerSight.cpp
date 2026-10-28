#include "HealerSight.h"
#include "BotGeometry.h"
#include "CommonRules.h"
#include "GroupCoordinator.h"
#include "KeepOut.h"
#include "PriestModule.h"
#include "SafePlace.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Bots
{
    namespace
    {
        // The rings round the tank, each this many yards, up to
        // TankSightMostYards, with this many spots on each; and the most
        // height of a spot over or under the tank.
        float const RingStepYards = 5.0f;
        int const SpotsPerRing = 16;
        float const SpotHeightYards = 8.0f;

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

        Position const self = view.GetPosition(view.GetSelf());
        Position const tankAt = view.GetPosition(tank);
        std::vector<Position> spots;
        for (float yards = RingStepYards; yards <= TankSightMostYards; yards += RingStepYards)
            for (int i = 0; i < SpotsPerRing; ++i)
            {
                float const angle = float(i) * 2.0f * Pi / float(SpotsPerRing);
                Position const at = view.GetGround(
                    {tankAt.x + std::cos(angle) * yards, tankAt.y + std::sin(angle) * yards, tankAt.z});
                if (std::fabs(at.z - tankAt.z) <= SpotHeightYards
                    && view.GetSpellDistanceFrom(at, tank) <= TankSightMostYards)
                    spots.push_back(at);
            }
        std::stable_sort(spots.begin(), spots.end(), [&self](Position const& a, Position const& b) {
            return Distance2d(self, a) < Distance2d(self, b);
        });

        SafeMoveRules rules;
        rules.inSightOf = {tank};
        rules.enemies = EnemiesOutsideTheFight(view, tankAt, FightTarget(context));
        return FirstSafePlace(view, spots, rules, spot);
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
}
