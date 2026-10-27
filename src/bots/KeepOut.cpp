#include "KeepOut.h"
#include "BotGeometry.h"
#include "FightSpot.h"
#include "GroupCoordinator.h"
#include "SafePlace.h"

#include <algorithm>
#include <cmath>

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

    Guid FightTarget(BotContext const& context)
    {
        Guid const focus = context.group.GetFocusTarget();
        return focus != NoGuid ? focus : context.group.GetPull().target;
    }

    bool InOtherPacksReach(BotContext const& context, Position const& at)
    {
        return PacksPulledAt(context.view, at, FightTarget(context)) > 0;
    }

    namespace
    {
        // A step out ends this far out of the aggro distance and margin.
        float const StepOutSpareYards = 1.0f;

        // The enemies outside the fight that the bot pulls where it
        // stands, nearest first. None when the rule does not apply.
        std::vector<NearEnemy> EnemiesTooNear(BotContext const& context)
        {
            Guid const self = context.view.GetSelf();
            PullState const pull = context.group.GetPull();
            // In the pull itself, the fight spot and the shoot spot keep
            // the puller out (FightSpot.h), and the member spots the others.
            if (pull.phase != PullPhase::Holding && !context.view.IsInCombat(self))
                return {};
            if (pull.goesIn && pull.puller == self)
                return {};
            Position const at = context.view.GetPosition(self);
            if (!InOtherPacksReach(context, at))
                return {};
            std::vector<NearEnemy> near;
            for (NearEnemy const& enemy : EnemiesOutsideTheFight(context.view, at, FightTarget(context)))
                if (PullsAnEnemy({enemy}, at))
                    near.push_back(enemy);
            std::sort(near.begin(), near.end(), [&at](NearEnemy const& a, NearEnemy const& b) {
                return Distance3d(a.position, at) < Distance3d(b.position, at);
            });
            return near;
        }

        // The places to step out to: straight away from each enemy too near,
        // to StepOutSpareYards out of its reach; then the tank, and the
        // fight spot of the pull.
        std::vector<Position> StepOutPlaces(BotContext const& context, std::vector<NearEnemy> const& tooNear)
        {
            Position const at = context.view.GetPosition(context.view.GetSelf());
            std::vector<Position> places;
            for (NearEnemy const& enemy : tooNear)
            {
                float const yards = Distance2d(enemy.position, at);
                if (yards < 0.1f)
                    continue;
                float const reach = enemy.aggroYards + AggroMarginYards + StepOutSpareYards;
                places.push_back(context.view.GetGround({enemy.position.x + (at.x - enemy.position.x) / yards * reach,
                    enemy.position.y + (at.y - enemy.position.y) / yards * reach, at.z}));
            }
            Guid const tank = context.group.GetTank();
            if (tank != NoGuid && tank != context.view.GetSelf())
                places.push_back(context.view.GetPosition(tank));
            PullState const pull = context.group.GetPull();
            if (pull.hasFightSpot)
                places.push_back(pull.fightSpot);
            return places;
        }

        // The safe place to step out to (FirstSafePlace). False when the bot
        // stands out of reach, or finds no safe place: it then keeps its
        // fight, so that a member never freezes in place.
        bool FindStepOut(BotContext const& context, Position& place)
        {
            std::vector<NearEnemy> const tooNear = EnemiesTooNear(context);
            if (tooNear.empty())
                return false;
            SafeMoveRules rules;
            rules.enemies = EnemiesOutsideTheFight(context.view,
                context.view.GetPosition(context.view.GetSelf()), FightTarget(context));
            return FirstSafePlace(context.view, StepOutPlaces(context, tooNear), rules, place);
        }
    }

    std::shared_ptr<Strategy const> MakeKeepOutStrategy()
    {
        Rule keepOut{KeepOutRule, Priority::Group + 82,
            [](BotContext const& context) {
                Position place{};
                return FindStepOut(context, place);
            },
            {[](BotContext const&) { return true; },
                [](BotContext const& context) {
                    Position place{};
                    if (FindStepOut(context, place))
                        context.actions.MoveTo(place);
                }}};
        return std::make_shared<Strategy const>(Strategy{"keep out", {keepOut}});
    }
}
