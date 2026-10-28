#include "TrailFollow.h"
#include "BotGeometry.h"
#include "BotView.h"
#include "CommonRules.h"
#include "GroupCoordinator.h"

#include <algorithm>
#include <cmath>

namespace Bots
{
    namespace
    {
        // A following bot follows the tank within this distance, as "follow
        // the player" does (ClassModules.cpp).
        float const TrailReachYards = 100.0f;

        BotRole const Roles[] = {BotRole::Tank, BotRole::Healer, BotRole::Melee, BotRole::Ranged};

        bool RoleOf(GroupCoordinator const& group, Guid member, BotRole& role)
        {
            for (BotRole const each : Roles)
                if (group.HasRole(member, each))
                {
                    role = each;
                    return true;
                }
            return false;
        }

        Position Lerp(Position const& a, Position const& b, float t)
        {
            return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
        }

        // A place on the way: `leg` is the index of the way point before it.
        struct WayPlace
        {
            Position at;
            std::size_t leg;
            float alongYards;
        };

        // The place `yards` along `way` from its start (2d). Past its end,
        // the way goes on in the direction of its last leg, or toward
        // `toward` for a way of one point.
        WayPlace PlaceAlong(std::vector<Position> const& way, float yards, Position const& toward, float& angle)
        {
            float walked = 0.0f;
            for (std::size_t i = 1; i < way.size(); ++i)
            {
                float const leg = Distance2d(way[i - 1], way[i]);
                if (leg > 0.0f && walked + leg >= yards)
                {
                    angle = std::atan2(way[i].y - way[i - 1].y, way[i].x - way[i - 1].x);
                    return {Lerp(way[i - 1], way[i], (yards - walked) / leg), i - 1, yards};
                }
                walked += leg;
            }
            Position const& last = way.back();
            Position const& from = way.size() > 1 ? way[way.size() - 2] : last;
            Position const& to = way.size() > 1 ? last : toward;
            angle = Distance2d(from, to) > 0.0f ? std::atan2(to.y - from.y, to.x - from.x) : -Pi / 2.0f;
            float const rest = yards - walked;
            return {{last.x + std::cos(angle) * rest, last.y + std::sin(angle) * rest, last.z}, way.size() - 1, yards};
        }

        // The place of the way nearest to `p`.
        WayPlace NearestOnWay(std::vector<Position> const& way, Position const& p)
        {
            WayPlace best{way.front(), 0, 0.0f};
            float bestYards = Distance2d(p, way.front());
            float walked = 0.0f;
            for (std::size_t i = 1; i < way.size(); ++i)
            {
                Position const& a = way[i - 1];
                Position const& b = way[i];
                float const leg = Distance2d(a, b);
                float t = 0.0f;
                if (leg > 0.0f)
                    t = std::max(0.0f, std::min(1.0f, ((p.x - a.x) * (b.x - a.x) + (p.y - a.y) * (b.y - a.y)) / (leg * leg)));
                Position const at = Lerp(a, b, t);
                float const yards = Distance2d(p, at);
                if (yards < bestYards)
                {
                    bestYards = yards;
                    best = {at, i - 1, walked + leg * t};
                }
                walked += leg;
            }
            return best;
        }

        float DistanceToSegment2d(Position const& p, Position const& a, Position const& b)
        {
            return DistanceToSegment({p.x, p.y, 0.0f}, {a.x, a.y, 0.0f}, {b.x, b.y, 0.0f});
        }

        // The bots of the group (a member with a role), self too, by guid.
        std::vector<Guid> GroupBots(BotContext const& context)
        {
            std::vector<Guid> bots{context.view.GetSelf()};
            BotRole role;
            for (Guid const member : context.view.GetGroupMembers())
                if (member != context.view.GetSelf() && RoleOf(context.group, member, role))
                    bots.push_back(member);
            std::sort(bots.begin(), bots.end());
            return bots;
        }

        // The tank's trail. In the tank's fight the trail is empty: then the
        // cleared way of the fight, cut back to where the tank stands
        // (spec #188 fix-up S3, a retreat pull).
        std::vector<Position> TrailNow(GroupCoordinator const& group, Position const& tank)
        {
            std::vector<Position> trail = group.GetTrail();
            if (!trail.empty())
                return trail;
            Trail fight;
            for (Position const& at : group.GetFightWay())
                fight.Note(at);
            fight.CutBackTo(tank);
            return fight.Points();
        }

        // The spot of this bot, and the way it walks. False with no role.
        bool MyTrailSpot(BotContext const& context, std::vector<Position>& way, Position& spot)
        {
            Guid const self = context.view.GetSelf();
            BotRole role;
            if (!RoleOf(context.group, self, role))
                return false;
            std::vector<Guid> const bots = GroupBots(context);
            std::size_t sideIndex = 0, inRole = 0, roleCount = 0;
            for (std::size_t i = 0; i < bots.size(); ++i)
            {
                BotRole other;
                if (bots[i] == self)
                {
                    sideIndex = i;
                    inRole = roleCount;
                }
                if (RoleOf(context.group, bots[i], other) && other == role)
                    ++roleCount;
            }
            Position const tank = context.view.GetPosition(context.group.GetTank());
            way = TankWay(tank, TrailNow(context.group, tank));
            spot = FindTrailSpot(context.view, way, context.view.GetPosition(self), role, inRole, roleCount, sideIndex);
            return true;
        }

        bool ShouldFollowTheTrail(BotContext const& context)
        {
            Guid const self = context.view.GetSelf();
            Guid const tank = context.group.GetTank();
            if (!FollowsTheTankTrail(context) || context.command != CommandState::Follow || context.startWaits
                // Spec #188 fix-up (S3): the bot's own combat only. A bot not
                // in the fight yet walks the trail behind the tank, also
                // while the tank or the group fights (a retreat pull).
                || context.view.IsInCombat(self)
                // A damage bot on its way to its target.
                || IsAlive(context.view, context.view.GetChase().target))
                return false;
            std::vector<Position> way;
            Position spot;
            // A bot in place that still follows the tank (a `follow`
            // command starts the follow of the game) stops there.
            return MyTrailSpot(context, way, spot)
                && (Distance2d(context.view.GetPosition(self), spot) > TrailInPlaceYards
                    || context.view.GetFollowTarget() == tank);
        }

        void WalkTheTrail(BotContext const& context)
        {
            std::vector<Position> way;
            Position spot;
            if (!MyTrailSpot(context, way, spot))
                return;
            Position const self = context.view.GetPosition(context.view.GetSelf());
            if (Distance2d(self, spot) <= TrailInPlaceYards)
            {
                context.actions.Stand();
                return;
            }
            context.actions.KeepMovingTo(NextTrailStep(way, self, spot));
        }
    }

    TrailBand TrailBandOf(BotRole role)
    {
        switch (role)
        {
            case BotRole::Healer:
                return HealerTrailBand;
            case BotRole::Ranged:
                return RangedTrailBand;
            case BotRole::Melee:
            case BotRole::Tank:
                break;
        }
        return MeleeTrailBand;
    }

    std::vector<Position> TankWay(Position const& tank, std::vector<Position> const& trail)
    {
        std::vector<Position> way{tank};
        for (std::size_t i = trail.size(); i-- > 0;)
            if (Distance2d(way.back(), trail[i]) > 0.1f)
                way.push_back(trail[i]);
        return way;
    }

    Position FindTrailSpot(BotView const& view, std::vector<Position> const& way, Position const& self, BotRole role,
        std::size_t inRole, std::size_t roleCount, std::size_t sideIndex)
    {
        TrailBand const band = TrailBandOf(role);
        std::size_t const count = std::max(roleCount, inRole + 1);
        float const yards =
            band.nearYards + (band.farYards - band.nearYards) * float(inRole + 1) / float(count + 1);
        float angle = 0.0f;
        WayPlace const place = PlaceAlong(way, yards, self, angle);
        // Sides in turn: right, left, then one step farther out.
        float const side = TrailSideStepYards * float(sideIndex / 2 + 1);
        float const sideAngle = angle + (sideIndex % 2 == 0 ? -Pi / 2.0f : Pi / 2.0f);
        // Short of a wall, by half a step.
        float const room = view.GetWallDistance(place.at, sideAngle, side + TrailSideStepYards / 2.0f);
        float const out = std::max(0.0f, std::min(side, room - TrailSideStepYards / 2.0f));
        return view.GetGround({place.at.x + std::cos(sideAngle) * out, place.at.y + std::sin(sideAngle) * out, place.at.z});
    }

    Position NextTrailStep(std::vector<Position> const& way, Position const& self, Position const& spot)
    {
        WayPlace const from = NearestOnWay(way, self);
        WayPlace const to = NearestOnWay(way, spot);
        if (Distance2d(self, from.at) > TrailJoinYards)
            return spot;
        // The way points between the bot and the spot, in walk order, then
        // the spot.
        std::vector<Position> steps;
        if (from.alongYards <= to.alongYards)
        {
            for (std::size_t i = from.leg + 1; i <= to.leg && i < way.size(); ++i)
                steps.push_back(way[i]);
        }
        else
        {
            for (std::size_t i = from.leg; i > to.leg; --i)
                steps.push_back(way[i]);
        }
        steps.push_back(spot);
        // The farthest step that the bot reaches in a straight line that
        // cuts no corner.
        for (std::size_t k = steps.size(); k-- > 0;)
        {
            bool straight = true;
            for (std::size_t j = 0; j < k && straight; ++j)
                straight = DistanceToSegment2d(steps[j], self, steps[k]) <= TrailCornerCutYards;
            if (straight)
                return steps[k];
        }
        return steps.front();
    }

    void WatchTankTrail(BotView const& view, GroupCoordinator& group)
    {
        if (!group.IsPlayerTankGroup())
            return;
        Guid const tank = group.GetTank();
        // A bot that does not find the tank (no health, place 0,0,0) leaves
        // the trail to the bots that see it: its note would clear it.
        if (view.GetHealth(tank).max == 0)
            return;
        group.KeepTrail(view.GetPosition(tank), view.IsInCombat(tank) || !IsAlive(view, tank));
    }

    bool FollowsTheTankTrail(BotContext const& context)
    {
        return context.group.IsPlayerTankGroup() && context.commandPlayer != NoGuid
            && context.commandPlayer == context.group.GetTank();
    }

    Rule TrailFollowRule(int priority)
    {
        Rule rule;
        rule.name = FollowTankTrailRule;
        rule.priority = priority;
        rule.trigger = ShouldFollowTheTrail;
        rule.action.canDo = [](BotContext const& context) {
            Guid const tank = context.group.GetTank();
            return IsAlive(context.view, tank) && context.view.GetDistance(tank) <= TrailReachYards;
        };
        rule.action.run = WalkTheTrail;
        return rule;
    }
}
