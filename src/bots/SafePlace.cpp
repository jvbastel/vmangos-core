#include "SafePlace.h"
#include "BotGeometry.h"

#include <algorithm>
#include <cmath>

namespace Bots
{
    namespace
    {
        // FindSafeCastPlace: the rings round the unit, as parts of the cast
        // range; the spots on each ring; the most height of a spot over or
        // under the unit.
        float const SafeRings[] = {0.9f, 0.6f, 0.3f};
        int const SafeSpotsPerRing = 16;
        float const SafeSpotHeightYards = 8.0f;
        // How far the bot looks for enemies: the walk, the cast range, and
        // this margin, up to the most.
        float const SafeSearchMarginYards = 40.0f;
        float const MaxSafeSearchYards = 150.0f;
        // A path step this much nearer to an enemy than the bot stands now
        // goes nearer.
        float const NearerYards = 0.5f;

        // From `at` to the enemy's center, as the game measures its aggro
        // distance (BasicAI::MoveInLineOfSight, no body size).
        float DistanceTo(NearEnemy const& enemy, Position const& at)
        {
            return Distance3d(enemy.position, at);
        }

        // A place within the aggro distance and the margin of an enemy
        // pulls it. On the path, a step outside the aggro distance that
        // goes no nearer to the enemy than `from` does not: the bot may
        // stand that near.
        bool Pulls(std::vector<NearEnemy> const& enemies, Position const& at, Position const* from)
        {
            for (NearEnemy const& enemy : enemies)
            {
                float const distance = DistanceTo(enemy, at);
                if (distance < enemy.aggroYards)
                    return true;
                if (distance < enemy.aggroYards + AggroMarginYards
                    && (!from || distance < DistanceTo(enemy, *from) - NearerYards))
                    return true;
            }
            return false;
        }

        bool IsSafePath(BotView const& view, std::vector<Position> const& path, SafeMoveRules const& rules)
        {
            Position const& from = path.front();
            Position unsafe;
            return !FindPointOnPath(path, SafePathStepYards,
                [&](Position const& step) { return view.IsHarmfulGround(step) || Pulls(rules.enemies, step, &from); },
                unsafe);
        }

        bool SeesAll(BotView const& view, Position const& place, SafeMoveRules const& rules)
        {
            for (Guid unit : rules.inSightOf)
                if (!view.CanSeeFrom(place, unit))
                    return false;
            for (Position const& other : rules.inSightOfPlaces)
                if (!view.CanSeeBetween(place, other))
                    return false;
            return true;
        }
    }

    bool PullsAnEnemy(std::vector<NearEnemy> const& enemies, Position const& at)
    {
        return Pulls(enemies, at, nullptr);
    }

    bool FirstSafePlace(BotView const& view, std::vector<Position> const& spots, SafeMoveRules const& rules,
        Position& place)
    {
        return FirstSafePlaceFrom(view, view.GetPosition(view.GetSelf()), spots, rules, place);
    }

    bool FirstSafePlaceFrom(BotView const& view, Position const& self, std::vector<Position> const& spots,
        SafeMoveRules const& rules, Position& place)
    {
        std::vector<Position> path;
        for (Position const& spot : spots)
        {
            if (Pulls(rules.enemies, spot, nullptr) || view.IsHarmfulGround(spot)
                || !SeesAll(view, spot, rules))
                continue;
            if (!view.FindGroundPath(self, spot, path) || path.empty() || !IsSafePath(view, path, rules))
                continue;
            place = spot;
            return true;
        }
        return false;
    }

    bool FindSafeCastPlace(BotView const& view, Guid unit, float yards, Position& place,
        std::vector<NearEnemy>* enemies, std::function<bool(Position const&)> const& accepts,
        std::vector<NearEnemy> const* keepOutOf)
    {
        Position const self = view.GetPosition(view.GetSelf());
        Position const target = view.GetPosition(unit);
        float const searchYards = Distance2d(self, target) + yards + SafeSearchMarginYards;
        if (searchYards > MaxSafeSearchYards)
            return false;

        std::vector<Position> spots;
        for (float ring : SafeRings)
            for (int i = 0; i < SafeSpotsPerRing; ++i)
            {
                float const angle = float(i) * 2.0f * Pi / float(SafeSpotsPerRing);
                Position const spot = view.GetGround(
                    {target.x + std::cos(angle) * ring * yards, target.y + std::sin(angle) * ring * yards, target.z});
                if (std::fabs(spot.z - target.z) <= SafeSpotHeightYards && (!accepts || accepts(spot)))
                    spots.push_back(spot);
            }
        std::stable_sort(spots.begin(), spots.end(), [&self](Position const& a, Position const& b) {
            return Distance2d(self, a) < Distance2d(self, b);
        });

        SafeMoveRules rules;
        rules.inSightOf = {unit};
        rules.enemies = keepOutOf ? *keepOutOf : view.GetEnemiesNear(searchYards);
        if (enemies)
            *enemies = rules.enemies;
        return FirstSafePlace(view, spots, rules, place);
    }
}
