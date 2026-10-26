#include "TaragamanReaction.h"
#include "CommonRules.h"
#include "BotGeometry.h"
#include "SafePlace.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace Bots
{
    namespace Taragaman
    {
        namespace
        {
            float const Degree = Pi / 180.0f;

            Guid Boss(BotContext const& context)
            {
                return context.group.GetBossFight().boss;
            }

            Position Self(BotContext const& context)
            {
                return context.view.GetPosition(context.view.GetSelf());
            }

            // The direction from `from` to `to`, in radians.
            float AngleTo(Position const& from, Position const& to)
            {
                return std::atan2(to.y - from.y, to.x - from.x);
            }

            Position Step(Position const& from, float angle, float yards)
            {
                return {from.x + yards * std::cos(angle), from.y + yards * std::sin(angle), from.z};
            }

            bool SelfAlive(BotContext const& context)
            {
                return context.view.GetLifeState(context.view.GetSelf()) == LifeState::Alive;
            }

            // The bot and the boss live, and the bot stands in reach of Fire
            // Nova.
            bool InFireNova(BotContext const& context)
            {
                Guid const boss = Boss(context);
                return SelfAlive(context) && IsAlive(context.view, boss)
                    && Distance2d(context.view.GetPosition(boss), Self(context)) < FireNovaYards;
            }

            // A place `SafeYards` from the boss, as straight away from him as
            // the walls allow: first the line from him through the bot, then
            // 30, 60, ... degrees off it. With walls all around, the most open
            // direction, short of its wall. The place must be safe
            // (SafePlace.h): no lava on it or on the way, and in his sight
            // (no cast from elsewhere); for the healer, in the tank's sight
            // too (no heal from elsewhere). False when no place is left.
            bool FindSafePlace(BotContext const& context, BotRole role, Position& found)
            {
                Position const boss = context.view.GetPosition(Boss(context));
                Position const self = Self(context);
                float const away = AngleTo(boss, self);
                // The open directions in their order, then the others, the
                // most open first.
                std::vector<Position> open;
                std::vector<std::pair<float, Position>> shortOfWall;
                for (int step = 0; step <= 6; ++step)
                    for (int side : {1, -1})
                    {
                        if (step == 0 && side == -1)
                            continue;
                        float const angle = away + side * step * 30.0f * Degree;
                        float const yards = context.view.GetWallDistance(boss, angle, SafeYards);
                        Position place = Step(boss, angle, yards >= SafeYards ? SafeYards : std::max(0.0f, yards - WallGapYards));
                        place.z = self.z;
                        place = context.view.GetGround(place);
                        if (yards >= SafeYards)
                            open.push_back(place);
                        else
                            shortOfWall.push_back({yards, place});
                    }
                std::stable_sort(shortOfWall.begin(), shortOfWall.end(),
                    [](std::pair<float, Position> const& a, std::pair<float, Position> const& b) { return a.first > b.first; });
                for (auto const& wall : shortOfWall)
                    open.push_back(wall.second);
                SafeMoveRules rules;
                rules.inSightOf = {Boss(context)};
                Guid const tank = context.group.GetTank();
                if (role == BotRole::Healer && tank != NoGuid && tank != context.view.GetSelf())
                    rules.inSightOf.push_back(tank);
                return FirstSafePlace(context.view, open, rules, found);
            }

            // The trigger finds the safe place and the action moves to it:
            // one search per think. The action runs in the same think as its
            // trigger (StrategyStack::Think), so the place it reads is the
            // one found now. Each bot has its own reaction, so no other bot
            // shares the place.
            Rule FireNovaRule(int priority, BotRole role)
            {
                auto const place = std::make_shared<Position>();
                Rule rule;
                rule.name = "stay out of fire nova";
                rule.priority = priority;
                rule.trigger = [role, place](BotContext const& context) {
                    return InFireNova(context) && FindSafePlace(context, role, *place);
                };
                rule.action.canDo = [](BotContext const&) { return true; };
                rule.action.run = [place](BotContext const& context) { context.actions.MoveTo(*place); };
                return rule;
            }

            // The direction of the nearest wall behind the tank, with the
            // boss in front: up to 60 degrees off the line from the boss
            // through the tank. `yards` gets its distance, `WallNearYards`
            // when no wall is near.
            float WallBehind(BotContext const& context, float& yards)
            {
                Position const self = Self(context);
                float const away = AngleTo(context.view.GetPosition(Boss(context)), self);
                float bestAngle = away;
                yards = WallNearYards;
                for (int step = -4; step <= 4; ++step)
                {
                    float const angle = away + step * 15.0f * Degree;
                    float const wall = context.view.GetWallDistance(self, angle, WallNearYards);
                    if (wall < yards)
                    {
                        yards = wall;
                        bestAngle = angle;
                    }
                }
                return bestAngle;
            }

            // The tank holds the boss, a wall is near behind it, and its back
            // is not at the wall yet.
            bool ShouldGoToWall(BotContext const& context)
            {
                Guid const boss = Boss(context);
                if (!SelfAlive(context) || !IsAlive(context.view, boss)
                    || context.view.GetVictim(boss) != context.view.GetSelf())
                    return false;
                float yards = 0.0f;
                WallBehind(context, yards);
                return yards < WallNearYards && yards > AtWallYards;
            }

            Rule MoveRule(std::string name, int priority, Trigger trigger, Position (*place)(BotContext const&))
            {
                Rule rule;
                rule.name = std::move(name);
                rule.priority = priority;
                rule.trigger = std::move(trigger);
                rule.action.canDo = [](BotContext const&) { return true; };
                rule.action.run = [place](BotContext const& context) { context.actions.MoveTo(place(context)); };
                return rule;
            }

            Position WallPlace(BotContext const& context)
            {
                float yards = 0.0f;
                float const angle = WallBehind(context, yards);
                return Step(Self(context), angle, yards - WallGapYards);
            }
        }

        std::shared_ptr<Strategy const> MakeReaction(BotRole role)
        {
            int const P = Priority::Boss;
            std::vector<Rule> rules;
            if (role == BotRole::Ranged || role == BotRole::Healer)
                rules.push_back(FireNovaRule(P + 50, role));
            if (role == BotRole::Tank)
                rules.push_back(MoveRule("back to the wall", P + 50, ShouldGoToWall, &WallPlace));
            return std::make_shared<Strategy const>(Strategy{"taragaman the hungerer", std::move(rules)});
        }
    }
}
