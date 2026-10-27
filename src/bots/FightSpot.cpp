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

        // The other packs that the pull brings with the fight at `spot`.
        auto extraAt = [&](Position const& spot) {
            std::uint32_t extra = assisting;
            for (std::vector<NearEnemy> const* pack : avoidable)
                if (PullsAnEnemy(*pack, spot) || Near(*pack, spot, AssistanceYards))
                    ++extra;
            return extra;
        };

        FightSpot result;
        bool const casterPack = IsCasterPack(view, targetPack);
        // The line-of-sight pull of a caster pack (jvbastel/WOWAI#121): the
        // first clean spot that the caster cannot see from its spawn, with
        // a shot from within LineOfSightShotYards of it that pulls no other
        // pack: where the tank stands, or a spot of the trail.
        if (casterPack)
            for (Position const& spot : spots)
            {
                if (extraAt(spot) != 0 || view.CanSeeBetween(targetAt, spot))
                    continue;
                for (Position const& shootAt : spots)
                    if (Distance3d(shootAt, spot) <= LineOfSightShotYards && InShotFrom(view, shootAt, target)
                        && (&shootAt == &spots.front() || !PullsAnEnemy(otherEnemies, shootAt)))
                    {
                        result.spot = spot;
                        result.clean = true;
                        result.lineOfSight = true;
                        result.hasShootSpot = &shootAt != &spots.front();
                        if (result.hasShootSpot)
                            result.shootSpot = shootAt;
                        return result;
                    }
            }

        // The pull from range: from where the tank stands, else from the
        // newest trail spot in shot that pulls no other pack, else from a
        // safe place round the target.
        bool canShoot = InShotFrom(view, selfAt, target);
        for (std::size_t i = 1; !canShoot && i < spots.size(); ++i)
            if (InShotFrom(view, spots[i], target) && !PullsAnEnemy(otherEnemies, spots[i]))
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
        // Last, a target on a ledge that no safe place sees (#122): the
        // tank closes in along its path until it has a shot. No checked
        // way, so the spot is not clean.
        if (!canShoot)
            result.hasShootSpot = FindShotOnPath(view, target, result.shootSpot);

        // A caster pack with no line-of-sight spot may stay at its spawn,
        // and the fight then goes there (the fallback of WarriorModule.h):
        // the spawn area counts too, so the pull warns when it is unsafe.
        std::uint32_t const atSpawn = casterPack ? extraAt(targetAt) : 0;
        bool found = false;
        for (Position const& spot : spots)
        {
            std::uint32_t const extra = std::max(extraAt(spot), atSpawn);
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
