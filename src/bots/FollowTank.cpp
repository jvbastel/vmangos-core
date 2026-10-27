#include "FollowTank.h"
#include "BotGeometry.h"
#include "CommonRules.h"
#include "GroupRules.h"
#include "SafePlace.h"

#include <algorithm>
#include <vector>

namespace Bots
{
    namespace
    {
        // How far the bot looks for enemies past the length of its path,
        // and the most.
        float const SearchMarginYards = 40.0f;
        float const MaxSearchYards = 150.0f;
        // An enemy this near to an enemy in the fight is that enemy.
        float const SameEnemyYards = 1.0f;
        // A spot this near to the bot is no move.
        float const NoMoveYards = 2.0f;

        // The tank of a pull that is on, and that another bot can follow.
        // The focus of the pull can die while the pack still fights: the
        // tank ends the pull and fights back the next mob. That fight goes
        // on as long as the tank fights a living focus.
        bool PullToFollow(BotContext const& context, Guid& tank)
        {
            PullState const pull = context.group.GetPull();
            tank = pull.puller;
            if (pull.phase == PullPhase::None)
            {
                tank = context.group.GetTank();
                if (!IsAlive(context.view, context.group.GetFocusTarget()) || !context.view.IsInCombat(tank))
                    return false;
            }
            return tank != NoGuid && tank != context.view.GetSelf() && context.command != CommandState::Come
                && IsAlive(context.view, tank);
        }

        // A bot that follows a group member already (FollowsAMember) keeps
        // that walk: a second walk would take turns with that one.
        bool ShouldFollowTank(BotContext const& context)
        {
            Guid tank = NoGuid;
            return PullToFollow(context, tank) && !FollowsAMember(context)
                && context.view.GetDistance(tank) > FollowTankYards;
        }

        // The enemies near the bot that a walk must not pull: all but those
        // that attack a group member.
        std::vector<NearEnemy> EnemiesToKeepOff(BotContext const& context, float yards)
        {
            std::vector<Position> fighting;
            for (Guid enemy : EnemiesInFight(context))
                if (context.view.GetVictim(enemy) != NoGuid)
                    fighting.push_back(context.view.GetPosition(enemy));
            std::vector<NearEnemy> enemies = context.view.GetEnemiesNear(yards);
            enemies.erase(std::remove_if(enemies.begin(), enemies.end(),
                              [&fighting](NearEnemy const& enemy) {
                                  return std::any_of(fighting.begin(), fighting.end(), [&enemy](Position const& at) {
                                      return Distance3d(at, enemy.position) < SameEnemyYards;
                                  });
                              }),
                enemies.end());
            return enemies;
        }

        // The spots on `path` (from the bot to the tank), from
        // FollowTankNearYards before its end, back to the bot in steps.
        std::vector<Position> SpotsBehindTank(BotContext const& context, std::vector<Position> const& path)
        {
            std::vector<Position> spots;
            Position const self = path.front();
            float back = 0.0f; // Path length from the tank to path[i].
            float next = FollowTankNearYards;
            for (std::size_t i = path.size() - 1; i > 0; --i)
            {
                Position const& a = path[i];
                Position const& b = path[i - 1];
                float const leg = Distance3d(a, b);
                while (leg > 0.0f && next <= back + leg)
                {
                    float const t = (next - back) / leg;
                    Position const spot = context.view.GetGround(
                        {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t});
                    if (Distance2d(spot, self) < NoMoveYards)
                        return spots;
                    spots.push_back(spot);
                    next += FollowTankStepYards;
                }
                back += leg;
            }
            return spots;
        }

        bool FindFollowPlace(BotContext const& context, Position& place)
        {
            Guid tank = NoGuid;
            PullToFollow(context, tank);
            std::vector<Position> path;
            if (!context.view.FindGroundPath(context.view.GetPosition(context.view.GetSelf()),
                    context.view.GetPosition(tank), path) || path.size() < 2)
                return false;
            float const length = PathLength(path);
            SafeMoveRules rules;
            rules.inSightOf = {tank};
            rules.enemies = EnemiesToKeepOff(context, std::min(length + SearchMarginYards, MaxSearchYards));
            return FirstSafePlace(context.view, SpotsBehindTank(context, path), rules, place);
        }
    }

    bool StaysNearThePull(BotContext const& context)
    {
        Guid tank = NoGuid;
        return PullToFollow(context, tank) && context.view.GetDistance(tank) <= FollowTankYards;
    }

    Rule FollowTankRule(int priority)
    {
        Rule rule;
        rule.name = "follow the tank to the pull";
        rule.priority = priority;
        rule.trigger = ShouldFollowTank;
        rule.action.canDo = [](BotContext const& context) {
            Position place{};
            return FindFollowPlace(context, place);
        };
        rule.action.run = [](BotContext const& context) {
            Position place{};
            if (FindFollowPlace(context, place))
                context.actions.KeepMovingTo(place);
        };
        return rule;
    }
}
