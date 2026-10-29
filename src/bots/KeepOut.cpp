#include "KeepOut.h"
#include "BotGeometry.h"
#include "CommonRules.h"
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

        // The tank hits an add on the healer that stands in the reach of
        // another pack, and the healer cannot step out: it goes in.
        bool TankHitsAnAddOfTheHealer(BotContext const& context, Guid member)
        {
            Guid const victim = context.view.GetVictim(member);
            return member == context.group.GetTank() && victim != NoGuid && TankGoesInForTheHealer(context, victim);
        }

        // The enemies outside the fight that member `member` pulls where it
        // stands, nearest first. None when the rule does not apply.
        std::vector<NearEnemy> EnemiesTooNear(BotContext const& context, Guid member)
        {
            PullState const pull = context.group.GetPull();
            // In the pull itself, the fight spot and the shoot spot keep
            // the puller out (FightSpot.h), and the member spots the others.
            if (pull.phase != PullPhase::Holding && !context.view.IsInCombat(member))
                return {};
            // The exceptions: the puller that goes in to a caster after its
            // warn, and the tank that goes in for the healer after its warn.
            if ((pull.goesIn && pull.puller == member) || TankHitsAnAddOfTheHealer(context, member))
                return {};
            // A melee member on an enemy that the tank holds in this reach,
            // also on its way in: the reach begins before melee contact.
            if (member != context.group.GetTank() && context.group.HasRole(member, BotRole::Melee)
                && TankHoldsEnemyInOtherPacksReach(context, context.view.GetVictim(member)))
                return {};
            // One reach rule with the fight spot search (FightSpot.h): it
            // chose the puller's fight spot and its shot, and warned when
            // they bring extra packs. While the puller has its fight spot (it
            // pulls, or walks back to it), keep-out leaves it to that choice:
            // else the two rules switch back and forth (t93-final2: 239
            // switches with the walk back).
            if (pull.phase != PullPhase::None && pull.puller == member && pull.hasFightSpot)
                return {};
            Position const at = context.view.GetPosition(member);
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
        std::vector<Position> StepOutPlaces(BotContext const& context, Guid member, std::vector<NearEnemy> const& tooNear)
        {
            Position const at = context.view.GetPosition(member);
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
            if (tank != NoGuid && tank != member)
                places.push_back(context.view.GetPosition(tank));
            PullState const pull = context.group.GetPull();
            if (pull.hasFightSpot)
                places.push_back(pull.fightSpot);
            return places;
        }

        // The safe place for member `member` to step out to
        // (FirstSafePlace). False when it stands out of reach, or finds no
        // safe place: it then keeps its fight, so that a member never
        // freezes in place.
        bool FindStepOut(BotContext const& context, Guid member, Position& place)
        {
            std::vector<NearEnemy> const tooNear = EnemiesTooNear(context, member);
            if (tooNear.empty())
                return false;
            Position const at = context.view.GetPosition(member);
            SafeMoveRules rules;
            rules.enemies = EnemiesOutsideTheFight(context.view, at, FightTarget(context));
            return FirstSafePlaceFrom(context.view, at, StepOutPlaces(context, member, tooNear), rules, place);
        }
    }

    bool StuckInOtherPacksReach(BotContext const& context, Guid member)
    {
        Position place{};
        return !EnemiesTooNear(context, member).empty() && !FindStepOut(context, member, place);
    }

    bool TankGoesInForTheHealer(BotContext const& context, Guid enemy)
    {
        Guid const healer = context.group.GetHealer();
        return healer != NoGuid && context.view.GetVictim(enemy) == healer
            && InOtherPacksReach(context, context.view.GetPosition(enemy)) && StuckInOtherPacksReach(context, healer);
    }

    bool TankHoldsEnemyInOtherPacksReach(BotContext const& context, Guid enemy)
    {
        Guid const tank = context.group.GetTank();
        return IsAlive(context.view, tank) && context.view.GetVictim(enemy) == tank
            && context.view.GetDistanceBetween(enemy, tank) <= MeleeContactYards
            && InOtherPacksReach(context, context.view.GetPosition(tank));
    }

    std::shared_ptr<Strategy const> MakeKeepOutStrategy()
    {
        Rule keepOut{KeepOutRule, Priority::Group + 82,
            [](BotContext const& context) {
                Position place{};
                return FindStepOut(context, context.view.GetSelf(), place);
            },
            {[](BotContext const&) { return true; },
                [](BotContext const& context) {
                    Position place{};
                    if (FindStepOut(context, context.view.GetSelf(), place))
                        context.actions.MoveTo(place);
                }}};
        return std::make_shared<Strategy const>(Strategy{"keep out", {keepOut}});
    }
}
