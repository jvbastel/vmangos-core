#include "StepBack.h"
#include "BotGeometry.h"
#include "CommonRules.h"
#include "DamageRules.h"
#include "KeepOut.h"
#include "SafePlace.h"

#include <algorithm>
#include <cmath>

namespace Bots
{
    namespace
    {
        // The bot looks this far round it for the frozen enemies that a
        // spot must keep out of reach of: the walk, the reach, and a margin.
        float const FrozenSearchYards = StepBackMostYards + StepBackYards + 5.0f;
        // The safe places round the bot: rings each this many yards, up to
        // StepBackMostYards, with this many spots on each.
        float const RingStepYards = 2.0f;
        int const SpotsPerRing = 16;

        bool OutOfReach(std::vector<NearEnemy> const& frozen, Position const& at)
        {
            return std::all_of(frozen.begin(), frozen.end(), [&at](NearEnemy const& enemy) {
                return Distance3d(enemy.position, at) >= StepBackYards;
            });
        }

        // The points of the cleared way `way` (oldest first) back from the
        // point nearest to the bot, while the walk there stays within
        // StepBackMostYards.
        std::vector<Position> WayBack(std::vector<Position> const& way, Position const& self)
        {
            std::vector<Position> spots;
            if (way.empty())
                return spots;
            std::size_t entry = 0;
            for (std::size_t i = 1; i < way.size(); ++i)
                if (Distance3d(self, way[i]) < Distance3d(self, way[entry]))
                    entry = i;
            float walked = Distance3d(self, way[entry]);
            for (std::size_t i = entry + 1; i-- > 0;)
            {
                if (i < entry)
                    walked += Distance3d(way[i + 1], way[i]);
                if (walked > StepBackMostYards)
                    break;
                spots.push_back(way[i]);
            }
            return spots;
        }

        // The spots round the bot on rings up to StepBackMostYards, the
        // nearest first.
        std::vector<Position> SpotsRound(BotView const& view, Position const& self)
        {
            std::vector<Position> spots;
            for (float yards = RingStepYards; yards <= StepBackMostYards; yards += RingStepYards)
                for (int i = 0; i < SpotsPerRing; ++i)
                {
                    float const angle = float(i) * 2.0f * Pi / float(SpotsPerRing);
                    spots.push_back(view.GetGround(
                        {self.x + std::cos(angle) * yards, self.y + std::sin(angle) * yards, self.z}));
                }
            std::stable_sort(spots.begin(), spots.end(), [&self](Position const& a, Position const& b) {
                return Distance2d(self, a) < Distance2d(self, b);
            });
            return spots;
        }
    }

    std::vector<NearEnemy> FrozenEnemiesNear(BotView const& view, float yards)
    {
        Position const self = view.GetPosition(view.GetSelf());
        std::vector<NearEnemy> frozen;
        for (NearEnemy const& enemy : view.GetEnemiesNear(yards))
            if (Distance3d(self, enemy.position) <= yards && IsFrozen(view, enemy.guid))
                frozen.push_back(enemy);
        return frozen;
    }

    bool FindStepBackSpot(BotContext const& context, BotRole role, Position& spot)
    {
        BotView const& view = context.view;
        Position const self = view.GetPosition(view.GetSelf());
        std::vector<NearEnemy> const frozen = FrozenEnemiesNear(view, FrozenSearchYards);
        if (OutOfReach(frozen, self))
            return false;

        SafeMoveRules rules;
        rules.enemies = EnemiesOutsideTheFight(view, self, FightTarget(context));
        // No aggro distance: the path goes no nearer to a frozen enemy
        // within AggroMarginYards (SafePlace.h). OutOfReach keeps the spot
        // StepBackYards away.
        for (NearEnemy enemy : frozen)
        {
            enemy.aggroYards = 0.0f;
            rules.enemies.push_back(enemy);
        }
        Guid const tank = context.group.GetTank();
        bool const keepsTank = role == BotRole::Healer && tank != view.GetSelf() && IsAlive(view, tank);
        if (keepsTank)
            rules.inSightOf = {tank};
        auto const fits = [&](Position const& at) {
            return OutOfReach(frozen, at) && (!keepsTank || view.GetSpellDistanceFrom(at, tank) <= StepBackHealYards);
        };

        std::vector<Position> spots = WayBack(context.group.GetFightWay(), self);
        std::vector<Position> const round = SpotsRound(view, self);
        spots.insert(spots.end(), round.begin(), round.end());
        spots.erase(std::remove_if(spots.begin(), spots.end(), [&fits](Position const& at) { return !fits(at); }),
            spots.end());
        return FirstSafePlace(view, spots, rules, spot);
    }

    Rule StepBackRule(int priority, BotRole role)
    {
        Rule rule;
        rule.name = StepBackRuleName;
        rule.priority = priority;
        rule.trigger = [](BotContext const& context) {
            return IsAlive(context.view, context.view.GetSelf())
                && !FrozenEnemiesNear(context.view, StepBackYards).empty();
        };
        rule.action.canDo = [role](BotContext const& context) {
            Position spot;
            return FindStepBackSpot(context, role, spot);
        };
        rule.action.run = [role](BotContext const& context) {
            Position spot;
            if (FindStepBackSpot(context, role, spot))
                context.actions.KeepMovingTo(spot);
        };
        return rule;
    }
}
