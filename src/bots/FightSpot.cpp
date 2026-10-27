#include "FightSpot.h"
#include "BotGeometry.h"
#include "Ranged.h"

#include <algorithm>

namespace Bots
{
    namespace
    {
        // The enemies round the trail spots that count.
        float const EnemySearchYards = FightSpotSearchYards + 60.0f;
        // A trail spot this near to the last one is the same spot.
        float const SameSpotYards = 0.5f;
        // The enemies round a place that a member there could pull: more
        // than the aggro distance of any creature and the margin.
        float const PullReachYards = 50.0f;

        bool Near(std::vector<NearEnemy> const& pack, Position const& at, float yards)
        {
            for (NearEnemy const& enemy : pack)
                if (Distance3d(enemy.position, at) <= yards)
                    return true;
            return false;
        }

        // A member of `a` within `yards` of a member of `b`.
        bool Near(std::vector<NearEnemy> const& a, std::vector<NearEnemy> const& b, float yards)
        {
            for (NearEnemy const& enemy : a)
                if (Near(b, enemy.position, yards))
                    return true;
            return false;
        }

        // The spots to try: the bot's place, then the trail from its
        // newest point, as far as FightSpotSearchYards.
        std::vector<Position> Candidates(Position const& self, std::vector<Position> const& trail)
        {
            std::vector<Position> spots = {self};
            for (auto point = trail.rbegin(); point != trail.rend(); ++point)
            {
                if (Distance3d(self, *point) > FightSpotSearchYards)
                    continue;
                if (Distance3d(spots.back(), *point) >= SameSpotYards)
                    spots.push_back(*point);
            }
            return spots;
        }

    }

    std::vector<std::vector<NearEnemy>> SplitIntoPacks(std::vector<NearEnemy> const& enemies)
    {
        std::vector<int> pack(enemies.size(), -1);
        int packs = 0;
        for (std::size_t first = 0; first < enemies.size(); ++first)
        {
            if (pack[first] >= 0)
                continue;
            pack[first] = packs;
            std::vector<std::size_t> open = {first};
            while (!open.empty())
            {
                std::size_t const at = open.back();
                open.pop_back();
                for (std::size_t other = 0; other < enemies.size(); ++other)
                    if (pack[other] < 0 && Distance3d(enemies[at].position, enemies[other].position) <= PackLinkYards)
                    {
                        pack[other] = packs;
                        open.push_back(other);
                    }
            }
            ++packs;
        }
        std::vector<std::vector<NearEnemy>> result(packs);
        for (std::size_t i = 0; i < enemies.size(); ++i)
            result[pack[i]].push_back(enemies[i]);
        return result;
    }

    TargetPack SplitTargetPack(std::vector<NearEnemy> const& enemies, Guid target, Position const& targetAt)
    {
        TargetPack split;
        split.pack = {{targetAt, 0.0f, target}};
        for (std::vector<NearEnemy>& pack : SplitIntoPacks(enemies))
        {
            bool const isTarget = std::any_of(pack.begin(), pack.end(), [target](NearEnemy const& enemy) {
                return enemy.guid == target;
            });
            if (isTarget)
                split.pack = std::move(pack);
            else
                split.others.push_back(std::move(pack));
        }
        return split;
    }

    std::vector<std::vector<NearEnemy>> PacksOutsideTheFight(BotView const& view, Position const& at)
    {
        Position const self = view.GetPosition(view.GetSelf());
        std::vector<NearEnemy> outside;
        for (NearEnemy const& enemy : view.GetEnemiesNear(Distance3d(self, at) + PullReachYards))
            if (!view.IsInCombat(enemy.guid))
                outside.push_back(enemy);
        return SplitIntoPacks(outside);
    }

    std::uint32_t PacksPulledAt(BotView const& view, Position const& at)
    {
        std::uint32_t pulled = 0;
        for (std::vector<NearEnemy> const& pack : PacksOutsideTheFight(view, at))
            if (PullsAnEnemy(pack, at))
                ++pulled;
        return pulled;
    }

    bool FindRunOutSpot(BotView const& view, std::vector<Position> const& way, Position const& from,
        Position const& focusAt, Position& spot)
    {
        bool found = false;
        for (auto point = way.rbegin(); point != way.rend(); ++point)
        {
            float const yards = Distance3d(from, *point);
            if (yards < RunOutStepYards || yards > FightSpotSearchYards
                || Distance3d(focusAt, *point) <= Distance3d(focusAt, from) || PacksPulledAt(view, *point) != 0)
                continue;
            if (!view.CanSeeBetween(focusAt, *point))
            {
                spot = *point;
                return true;
            }
            if (!found)
            {
                spot = *point;
                found = true;
            }
        }
        return found;
    }

    bool IsCasterPack(BotView const& view, std::vector<NearEnemy> const& pack)
    {
        return std::any_of(pack.begin(), pack.end(), [&view](NearEnemy const& enemy) {
            return view.GetMana(enemy.guid).max > 0;
        });
    }

    FightSpot FindFightSpot(BotView const& view, std::vector<Position> const& trail, Guid target)
    {
        Guid const self = view.GetSelf();
        Position const selfAt = view.GetPosition(self);
        Position const targetAt = view.GetPosition(target);

        TargetPack const split = SplitTargetPack(view.GetEnemiesNear(EnemySearchYards), target, targetAt);
        std::vector<NearEnemy> const& targetPack = split.pack;
        std::vector<std::vector<NearEnemy>> const& others = split.others;
        std::vector<NearEnemy> otherEnemies;
        std::uint32_t assisting = 0;
        std::vector<std::vector<NearEnemy> const*> avoidable;
        for (std::vector<NearEnemy> const& pack : others)
        {
            otherEnemies.insert(otherEnemies.end(), pack.begin(), pack.end());
            if (Near(pack, targetPack, AssistanceYards + AssistBodyYards))
                ++assisting;
            else
                avoidable.push_back(&pack);
        }

        std::vector<Position> const spots = Candidates(selfAt, trail);

        FightSpot result;
        for (NearEnemy const& enemy : targetPack)
            result.targetPack.push_back(enemy.guid);
        for (std::vector<NearEnemy> const& pack : others)
        {
            result.otherPacks.emplace_back();
            for (NearEnemy const& enemy : pack)
                result.otherPacks.back().push_back(enemy.guid);
        }
        // The other packs that the pull brings with the tank at `spot`.
        auto extraAt = [&](Position const& spot) {
            std::uint32_t extra = assisting;
            for (std::vector<NearEnemy> const* pack : avoidable)
                if (PullsAnEnemy(*pack, spot) || Near(*pack, spot, AssistanceYards))
                    ++extra;
            return extra;
        };
        // A shot from a spot that pulls no other pack.
        auto safeShot = [&](Position const& at) {
            return InShotFrom(view, at, target) && !PullsAnEnemy(otherEnemies, at);
        };

        // The line-of-sight pull of a caster pack: the first clean spot that
        // the caster cannot see, with a shot near it.
        if (IsCasterPack(view, targetPack))
            for (Position const& spot : spots)
            {
                if (extraAt(spot) != 0 || view.CanSeeBetween(targetAt, spot))
                    continue;
                auto nearSpot = [&spot](Position const& at) { return Distance3d(at, spot) <= LineOfSightShotYards; };
                bool shot = nearSpot(selfAt) && InShotFrom(view, selfAt, target);
                for (std::size_t i = 1; !shot && i < spots.size(); ++i)
                    if (nearSpot(spots[i]) && safeShot(spots[i]))
                    {
                        shot = true;
                        result.hasShootSpot = true;
                        result.shootSpot = spots[i];
                    }
                if (!shot
                    && FindSafeCastPlace(view, target, ShotMaxYards, result.shootSpot, nullptr,
                        [&](Position const& at) { return nearSpot(at) && InShotFrom(view, at, target); }))
                {
                    shot = true;
                    result.hasShootSpot = true;
                }
                if (shot)
                {
                    result.spot = spot;
                    result.clean = true;
                    result.lineOfSight = true;
                    return result;
                }
            }

        // The pull from range: from where the tank stands, else from the
        // newest trail spot in shot that pulls no other pack, else from a
        // safe place round the target.
        bool canShoot = InShotFrom(view, selfAt, target);
        for (std::size_t i = 1; !canShoot && i < spots.size(); ++i)
            if (safeShot(spots[i]))
            {
                canShoot = true;
                result.hasShootSpot = true;
                result.shootSpot = spots[i];
            }
        if (!canShoot
            && FindSafeCastPlace(view, target, ShotMaxYards, result.shootSpot, nullptr,
                [&view, target](Position const& at) { return InShotFrom(view, at, target); }))
        {
            canShoot = true;
            result.hasShootSpot = true;
        }

        bool found = false;
        for (Position const& spot : spots)
        {
            std::uint32_t const extra = extraAt(spot);
            if (extra == 0 && canShoot)
            {
                result.spot = spot;
                result.extraPacks = 0;
                result.clean = true;
                return result;
            }
            if (!found || extra < result.extraPacks)
            {
                result.spot = spot;
                result.extraPacks = extra;
                found = true;
            }
        }
        return result;
    }
}
