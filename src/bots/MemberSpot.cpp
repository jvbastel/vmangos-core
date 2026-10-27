#include "MemberSpot.h"
#include "BotGeometry.h"
#include "CommonRules.h"
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
            return yards >= MemberBackMinYards && yards <= MemberBackMaxYards
                && std::fabs(spot.z - fightSpot.z) <= MemberHeightYards;
        }

        bool FarFromTaken(Position const& spot, std::vector<Position> const& taken)
        {
            return std::all_of(taken.begin(), taken.end(), [&spot](Position const& other) {
                return Distance3d(spot, other) >= MemberSpreadYards;
            });
        }
    }

    bool FindMemberSpot(BotView const& view, BotRole role, Position const& fightSpot,
        std::vector<Position> const& trail, Guid target, std::vector<Position> const& taken,
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
            for (std::vector<NearEnemy> const& pack : SplitTargetPack(enemies, target, view.GetPosition(target)).others)
                rules.enemies.insert(rules.enemies.end(), pack.begin(), pack.end());
            for (float degrees : {0.0f, 90.0f, -90.0f})
                spots.push_back(Along(view, fightSpot, dx, dy, degrees, MeleeBackYards));
            spots.push_back(fightSpot);
        }
        else
        {
            rules.enemies = enemies;
            // The healer heals the tank at its fight spot.
            if (role == BotRole::Healer)
                rules.inSightOfPlaces = {fightSpot};
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

    namespace
    {
        // The bot joins a pull now: it lives, is in no fight, and is not
        // told to come.
        bool JoinsPulls(BotContext const& context)
        {
            Guid const self = context.view.GetSelf();
            return IsAlive(context.view, self) && !context.view.IsInCombat(self)
                && context.command != CommandState::Come;
        }
    }

    void NoteForPulls(BotContext const& context)
    {
        context.group.NoteBot(context.view.GetSelf(), context.nowMs, JoinsPulls(context));
    }

    std::vector<Guid> MembersNotInPlace(BotContext const& context)
    {
        Guid const self = context.view.GetSelf();
        std::vector<Guid> const members = context.view.GetGroupMembers();
        std::vector<MemberSpot> const spots = context.group.GetMemberSpots();
        std::vector<Guid> waited;
        for (Guid bot : context.group.GetPullMembers(context.nowMs, BotNoteMs))
        {
            if (bot == self || std::find(members.begin(), members.end(), bot) == members.end()
                || !IsAlive(context.view, bot))
                continue;
            auto const spot = std::find_if(spots.begin(), spots.end(), [bot](MemberSpot const& member) {
                return member.bot == bot;
            });
            if (spot == spots.end() || (spot->found && !spot->inPlace))
                waited.push_back(bot);
        }
        return waited;
    }

    bool TankWaitsForTheGroup(BotContext const& context)
    {
        PullState const pull = context.group.GetPull();
        return pull.phase == PullPhase::Pulling && pull.puller == context.view.GetSelf() && pull.hasFightSpot
            && context.nowMs - pull.waitFromMs < GroupWaitMs && !MembersNotInPlace(context).empty();
    }

    std::string GroupWaitLine(std::uint32_t members)
    {
        return "Waiting for " + std::to_string(members) + (members == 1 ? " member" : " members")
            + " to get in place.";
    }

    std::string PullWithoutLine(std::uint32_t members)
    {
        return "Pulling without " + std::to_string(members) + (members == 1 ? " member." : " members.");
    }

    namespace
    {
        // The bot's member spot in the pull, if it noted one.
        bool MySpot(BotContext const& context, MemberSpot& mine)
        {
            for (MemberSpot const& spot : context.group.GetMemberSpots())
                if (spot.bot == context.view.GetSelf())
                {
                    mine = spot;
                    return true;
                }
            return false;
        }

        // A pull is on, the tank has its fight spot, and the pack does not
        // hold on it yet. Not for the puller, a bot in a fight, nor a bot
        // told to come; nor once the bot found no safe spot.
        bool GoesToMemberSpot(BotContext const& context)
        {
            PullState const pull = context.group.GetPull();
            Guid const self = context.view.GetSelf();
            if (pull.phase != PullPhase::Pulling || !pull.hasFightSpot || pull.puller == self || !JoinsPulls(context))
                return false;
            MemberSpot mine;
            return !MySpot(context, mine) || (mine.found && !mine.inPlace);
        }

        void GoToMemberSpot(BotContext const& context, BotRole role)
        {
            Guid const self = context.view.GetSelf();
            // The trail holds still while the pull is on (Trail.h).
            std::vector<Position> const trail = context.group.GetTrail();
            MemberSpot const mine = context.group.ClaimMemberSpot(self,
                [&context, role, &trail](PullState const& pull, std::vector<Position> const& taken, Position& spot) {
                    return pull.hasFightSpot
                        && FindMemberSpot(context.view, role, pull.fightSpot, trail, pull.target, taken, spot);
                });
            if (!mine.found)
                return;
            if (Distance2d(context.view.GetPosition(self), mine.spot) <= AtSpotYards)
            {
                context.group.SetMemberInPlace(self);
                return;
            }
            context.actions.KeepMovingTo(mine.spot);
        }
    }

    bool HoldsMemberSpot(BotContext const& context)
    {
        PullState const pull = context.group.GetPull();
        MemberSpot mine;
        return pull.phase == PullPhase::Pulling && pull.hasFightSpot && pull.puller != context.view.GetSelf()
            && MySpot(context, mine) && mine.found && mine.inPlace;
    }

    std::shared_ptr<Strategy const> MakeMemberSpotStrategy(BotRole role)
    {
        if (role == BotRole::Tank)
            return nullptr;
        Rule rule;
        rule.name = GoToMemberSpotRule;
        rule.priority = Priority::Command + 33;
        rule.trigger = GoesToMemberSpot;
        rule.action.canDo = [](BotContext const&) { return true; };
        rule.action.run = [role](BotContext const& context) { GoToMemberSpot(context, role); };
        return std::make_shared<Strategy const>(Strategy{"member spot", {rule}});
    }
}
