#include "KeepOut.h"
#include "BotGeometry.h"
#include "FightSpot.h"
#include "SafePlace.h"

namespace Bots
{
    std::vector<NearEnemy> EnemiesOutsideTheFight(BotView const& view, Position const& at, Guid fightTarget)
    {
        Position const self = view.GetPosition(view.GetSelf());
        std::vector<NearEnemy> outside;
        for (NearEnemy const& enemy : view.GetEnemiesNear(Distance3d(self, at) + KeepOutSearchYards))
            if (!view.IsInCombat(enemy.guid))
                outside.push_back(enemy);
        if (fightTarget == NoGuid)
            return outside;
        // The target links its pack, in combat or not.
        Position const targetAt = view.GetPosition(fightTarget);
        outside.push_back({targetAt, 0.0f, fightTarget});
        std::vector<NearEnemy> others;
        for (std::vector<NearEnemy> const& pack : SplitTargetPack(outside, fightTarget, targetAt).others)
            others.insert(others.end(), pack.begin(), pack.end());
        return others;
    }

    std::uint32_t PacksPulledAt(BotView const& view, Position const& at, Guid fightTarget)
    {
        std::uint32_t pulled = 0;
        for (std::vector<NearEnemy> const& pack : SplitIntoPacks(EnemiesOutsideTheFight(view, at, fightTarget)))
            if (PullsAnEnemy(pack, at))
                ++pulled;
        return pulled;
    }
}
