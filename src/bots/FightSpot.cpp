#include "FightSpot.h"
#include "BotGeometry.h"
#include "Ranged.h"

#include <algorithm>
#include <iterator>

namespace Bots
{
    namespace
    {
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

    namespace
    {
        // The search of a pull of `target`, shared by the tank's choice
        // (FindFightSpot) and the judge's (FindPullOptions).
        struct PullSearch
        {
            BotView const& view;
            Guid target;
            Position targetAt;
            TargetPack split;
            std::vector<NearEnemy> otherEnemies;
            // Of split.others: the packs within the assistance radius of
            // the target's pack (each pull brings them), and the rest.
            std::vector<std::size_t> assisting;
            std::vector<std::size_t> avoidable;
            std::vector<Position> spots;
            bool casterPack = false;

            PullSearch(BotView const& view, std::vector<Position> const& trail, Guid target,
                std::vector<NearEnemy> const& enemies)
                : view(view), target(target), targetAt(view.GetPosition(target)),
                  split(SplitTargetPack(enemies, target, targetAt)),
                  spots(Candidates(view.GetPosition(view.GetSelf()), trail)),
                  casterPack(IsCasterPack(view, split.pack))
            {
                for (std::size_t pack = 0; pack < split.others.size(); ++pack)
                {
                    otherEnemies.insert(otherEnemies.end(), split.others[pack].begin(), split.others[pack].end());
                    if (Near(split.others[pack], split.pack, AssistanceYards + AssistBodyYards))
                        assisting.push_back(pack);
                    else
                        avoidable.push_back(pack);
                }
            }

            // The other packs (of split.others) that the pull brings with
            // the fight at `spot`.
            std::vector<std::size_t> ExtraAt(Position const& spot) const
            {
                std::vector<std::size_t> extra = assisting;
                for (std::size_t pack : avoidable)
                    if (PullsAnEnemy(split.others[pack], spot) || Near(split.others[pack], spot, AssistanceYards))
                        extra.push_back(pack);
                std::sort(extra.begin(), extra.end());
                return extra;
            }

            // A line-of-sight pull of a caster pack (jvbastel/WOWAI#121) at
            // `spot`: the caster cannot see it from its spawn, and a shot
            // goes from within LineOfSightShotYards of it that pulls no
            // other pack: where the tank stands, or a spot of the trail.
            // The shoot spot, or null.
            Position const* LineOfSightShot(Position const& spot) const
            {
                if (!casterPack || view.CanSeeBetween(targetAt, spot))
                    return nullptr;
                for (Position const& shootAt : spots)
                    if (Distance3d(shootAt, spot) <= LineOfSightShotYards && InShotFrom(view, shootAt, target)
                        && (&shootAt == &spots.front() || !PullsAnEnemy(otherEnemies, shootAt)))
                        return &shootAt;
                return nullptr;
            }
        };
    }

    FightSpot FindFightSpot(BotView const& view, std::vector<Position> const& trail, Guid target)
    {
        PullSearch const search(view, trail, target, view.GetEnemiesNear(FightSpotEnemyYards));
        std::vector<Position> const& spots = search.spots;
        Position const selfAt = spots.front();
        auto extraAt = [&search](Position const& spot) { return std::uint32_t(search.ExtraAt(spot).size()); };

        FightSpot result;
        for (NearEnemy const& enemy : search.split.pack)
            result.targetPack.push_back(enemy.guid);
        for (std::vector<NearEnemy> const& pack : search.split.others)
        {
            result.otherPacks.emplace_back();
            for (NearEnemy const& enemy : pack)
                result.otherPacks.back().push_back(enemy.guid);
        }
        // The line-of-sight pull of a caster pack first: its first clean
        // spot.
        for (Position const& spot : spots)
        {
            if (extraAt(spot) != 0)
                continue;
            if (Position const* shootAt = search.LineOfSightShot(spot))
            {
                result.spot = spot;
                result.clean = true;
                result.lineOfSight = true;
                result.hasShootSpot = shootAt != &spots.front();
                if (result.hasShootSpot)
                    result.shootSpot = *shootAt;
                return result;
            }
        }

        // The pull from range: from where the tank stands, else from the
        // newest trail spot in shot that pulls no other pack, else from a
        // safe place round the target.
        bool canShoot = InShotFrom(view, selfAt, target);
        for (std::size_t i = 1; !canShoot && i < spots.size(); ++i)
            if (InShotFrom(view, spots[i], target) && !PullsAnEnemy(search.otherEnemies, spots[i]))
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
        std::uint32_t const atSpawn = search.casterPack ? extraAt(search.targetAt) : 0;
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

    PullOptions FindPullOptions(BotView const& view, std::vector<Position> const& trail, Guid target)
    {
        PullSearch const search(view, trail, target, view.GetEnemiesNear(FightSpotEnemyYards));
        PullOptions result;
        result.split = search.split;
        for (Position const& spot : search.spots)
            if (search.LineOfSightShot(spot))
                result.options.push_back({PullWay::LineOfSight, spot, search.ExtraAt(spot)});
        // A fight spot needs a shot that the search trusts: from where the
        // tank stands, from a trail spot, or from a safe place round the
        // target.
        bool canShoot = false;
        Position shootAt{};
        for (std::size_t i = 0; !canShoot && i < search.spots.size(); ++i)
            canShoot = InShotFrom(view, search.spots[i], target)
                && (i == 0 || !PullsAnEnemy(search.otherEnemies, search.spots[i]));
        canShoot = canShoot || FindSafeCastPlace(view, target, ShotMaxYards, shootAt, nullptr,
            [&view, target](Position const& at) { return InShotFrom(view, at, target); });
        std::vector<std::size_t> const atSpawn =
            search.casterPack ? search.ExtraAt(search.targetAt) : std::vector<std::size_t>();
        if (canShoot)
            for (Position const& spot : search.spots)
            {
                // The fight of a caster pack may go to its spawn.
                std::vector<std::size_t> extra = search.ExtraAt(spot);
                std::vector<std::size_t> both;
                std::set_union(extra.begin(), extra.end(), atSpawn.begin(), atSpawn.end(), std::back_inserter(both));
                result.options.push_back({PullWay::FightSpot, spot, both});
            }
        return result;
    }
}
