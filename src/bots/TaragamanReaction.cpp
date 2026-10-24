#include "TaragamanReaction.h"
#include "CommonRules.h"

#include <algorithm>
#include <cmath>

namespace Bots
{
    namespace Taragaman
    {
        namespace
        {
            float const Pi = 3.14159265f;
            float const Degree = Pi / 180.0f;

            Guid Boss(BotContext const& context)
            {
                return context.group.GetBossFight().boss;
            }

            Position Self(BotContext const& context)
            {
                return context.view.GetPosition(context.view.GetSelf());
            }

            float Distance2d(Position const& a, Position const& b)
            {
                return std::hypot(b.x - a.x, b.y - a.y);
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
            // direction, short of its wall.
            Position SafePlace(BotContext const& context)
            {
                Position const boss = context.view.GetPosition(Boss(context));
                Position const self = Self(context);
                float const away = AngleTo(boss, self);
                float bestAngle = away;
                float bestYards = -1.0f;
                for (int step = 0; step <= 6; ++step)
                    for (int side : {1, -1})
                    {
                        if (step == 0 && side == -1)
                            continue;
                        float const angle = away + side * step * 30.0f * Degree;
                        float const open = context.view.GetWallDistance(boss, angle, SafeYards);
                        if (open >= SafeYards)
                        {
                            Position place = Step(boss, angle, SafeYards);
                            place.z = self.z;
                            return place;
                        }
                        if (open > bestYards)
                        {
                            bestYards = open;
                            bestAngle = angle;
                        }
                    }
                Position place = Step(boss, bestAngle, std::max(0.0f, bestYards - WallGapYards));
                place.z = self.z;
                return place;
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
                rules.push_back(MoveRule("stay out of fire nova", P + 50, InFireNova, &SafePlace));
            if (role == BotRole::Tank)
                rules.push_back(MoveRule("back to the wall", P + 50, ShouldGoToWall, &WallPlace));
            return std::make_shared<Strategy const>(Strategy{"taragaman the hungerer", std::move(rules)});
        }
    }
}
