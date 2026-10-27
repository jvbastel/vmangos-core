#include "MemberSpot.h"
#include "BotGeometry.h"
#include "FightSpot.h"
#include "SafePlace.h"

#include <algorithm>
#include <cmath>

namespace Bots
{
    namespace
    {
        // How far the bot looks for enemies past its spots, and the most.
        float const SearchMarginYards = 40.0f;
        float const MaxSearchYards = 150.0f;
        // Without a trail point in the band: spots on a fan back from the
        // fight spot, at these yards and these angles off the way back.
        float const FanYards[] = {20.0f, 15.0f, 25.0f};
        float const FanDegrees[] = {0.0f, 30.0f, -30.0f, 60.0f, -60.0f, 90.0f, -90.0f};

        // A unit vector on the ground from `from` to `to`; (-1, 0) when they
        // are on one spot.
        void Direction(Position const& from, Position const& to, float& dx, float& dy)
        {
            float const length = Distance2d(from, to);
            if (length < 0.01f)
            {
                dx = -1.0f;
                dy = 0.0f;
                return;
            }
            dx = (to.x - from.x) / length;
            dy = (to.y - from.y) / length;
        }

        Position Along(BotView const& view, Position const& from, float dx, float dy, float degrees, float yards)
        {
            float const angle = degrees * Pi / 180.0f;
            float const rx = dx * std::cos(angle) - dy * std::sin(angle);
            float const ry = dx * std::sin(angle) + dy * std::cos(angle);
            return view.GetGround({from.x + rx * yards, from.y + ry * yards, from.z});
        }

        bool InBand(Position const& spot, Position const& fightSpot)
        {
            float const yards = Distance3d(spot, fightSpot);
            return yards >= MemberBackMinYards && yards <= MemberBackMaxYards;
        }

        bool FarFromTaken(Position const& spot, std::vector<Position> const& taken)
        {
            return std::all_of(taken.begin(), taken.end(), [&spot](Position const& other) {
                return Distance3d(spot, other) >= MemberSpreadYards;
            });
        }

        // The enemies of all packs but the target's.
        std::vector<NearEnemy> OtherPacks(std::vector<NearEnemy> const& enemies, Guid target)
        {
            std::vector<NearEnemy> others;
            for (std::vector<NearEnemy> const& pack : SplitIntoPacks(enemies))
                if (std::none_of(pack.begin(), pack.end(), [target](NearEnemy const& enemy) { return enemy.guid == target; }))
                    others.insert(others.end(), pack.begin(), pack.end());
            return others;
        }
    }

    bool FindMemberSpot(BotView const& view, BotRole role, Position const& fightSpot,
        std::vector<Position> const& trail, Guid tank, Guid target, std::vector<Position> const& taken,
        Position& spot)
    {
        Position const self = view.GetPosition(view.GetSelf());
        float dx = 0.0f;
        float dy = 0.0f;
        // Back: from the target to the fight spot, and on.
        Direction(view.GetPosition(target), fightSpot, dx, dy);
        float const searchYards = std::min(Distance2d(self, fightSpot) + MemberBackMaxYards + SearchMarginYards,
            MaxSearchYards);
        std::vector<NearEnemy> const enemies = view.GetEnemiesNear(searchYards);

        SafeMoveRules rules;
        std::vector<Position> spots;
        if (role == BotRole::Melee)
        {
            rules.enemies = OtherPacks(enemies, target);
            for (float degrees : {0.0f, 90.0f, -90.0f})
                spots.push_back(Along(view, fightSpot, dx, dy, degrees, MeleeBackYards));
            spots.push_back(fightSpot);
        }
        else
        {
            rules.enemies = enemies;
            if (role == BotRole::Healer)
                rules.inSightOf = {tank};
            // The cleared way first, from the fight spot back.
            for (auto point = trail.rbegin(); point != trail.rend(); ++point)
                if (InBand(*point, fightSpot))
                    spots.push_back(*point);
            for (float yards : FanYards)
                for (float degrees : FanDegrees)
                {
                    Position const at = Along(view, fightSpot, dx, dy, degrees, yards);
                    if (InBand(at, fightSpot))
                        spots.push_back(at);
                }
            spots.erase(std::remove_if(spots.begin(), spots.end(),
                            [&taken](Position const& at) { return !FarFromTaken(at, taken); }),
                spots.end());
        }
        return FirstSafePlace(view, spots, rules, spot);
    }
}
