#ifndef WOWAI_BOTS_MEMBER_SPOT_H
#define WOWAI_BOTS_MEMBER_SPOT_H

#include "BotRecipe.h"
#include "BotView.h"
#include "Strategy.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// The member spots of a pull (jvbastel/WOWAI#106). Once the tank has picked
// its fight spot (FightSpot.h), each other bot goes to a spot of its own
// before the tank pulls:
// - melee damage: at the fight spot, a few yards back toward the cleared
//   way, so the pack comes to the tank first;
// - ranged damage and the healer: MemberBackMinYards to MemberBackMaxYards
//   back from the fight spot, toward the cleared way (the tank's trail, else
//   away from the target), at least MemberSpreadYards from the spot of each
//   other member, so one area attack cannot hit them all;
// - the healer's spot sees the tank.
// Each spot, and the path to it, keeps out of the aggro distance of each
// enemy and the margin (the safe place rule, SafePlace.h). Melee keeps out
// of the other packs only: it fights the target's pack at the fight spot.
// Each bot finds its own spot (the safe path starts where it stands) and
// notes it in the group coordinator; the tank pulls when each bot is in
// place, or after GroupWaitMs (TankWaitsForTheGroup). The player places
// themselves: only bots are waited for. The test driver of a scripted run
// is a bot like any other.
namespace Bots
{
    float const MemberBackMinYards = 15.0f;
    float const MemberBackMaxYards = 25.0f;
    float const MemberSpreadYards = 5.0f;
    // Melee stands this far back from the fight spot.
    float const MeleeBackYards = 3.0f;
    // Nearer than this to its spot, a bot is in place.
    float const MemberInPlaceYards = 2.0f;
    // The longest wait of the tank for the group.
    std::int64_t const GroupWaitMs = 10 * 1000;
    // A wait this long, or longer, the tank says in party chat (once a
    // pull). A shorter one goes by unnoticed.
    std::int64_t const GroupWaitSayMs = 3 * 1000;

    // The spot of the bot of role `role` for the pull of `target`, with the
    // tank's fight spot `fightSpot` and its trail `trail` (oldest first).
    // `taken`: the spots of the other members. False when no spot is safe.
    bool FindMemberSpot(BotView const& view, BotRole role, Position const& fightSpot,
        std::vector<Position> const& trail, Guid tank, Guid target, std::vector<Position> const& taken,
        Position& spot);

    // The bots that the puller waits for: each living bot of its group that
    // the coordinator knows (GroupCoordinator::NoteBot), but the puller,
    // that is not in place at its member spot, and has found one or not yet
    // looked for one.
    std::vector<Guid> MembersNotInPlace(BotContext const& context);

    // The tank waits for the group: a pull with a fight spot, with a member
    // not in place, for less than GroupWaitMs since the puller noted its
    // fight spot. A bot once in place is not waited for again.
    bool TankWaitsForTheGroup(BotContext const& context);

    // The party line of the tank that waits for `members` members.
    std::string GroupWaitLine(std::uint32_t members);

    // The bot stands at its member spot in the pull that is on: it holds
    // there, so the follow rules (FollowTank.h, "follow the player") and the
    // corpse loot leave it; a rest may go on there.
    bool HoldsMemberSpot(BotContext const& context);

    // The strategy "member spot" of a bot of role `role`: its rule "go to
    // the member spot" at Priority::Command + 33, over the rest after a fight
    // (a move ends the meal; the bot rests again at its spot), the follow
    // rules, and the loot, and under the class rules. Null for the tank.
    std::shared_ptr<Strategy const> MakeMemberSpotStrategy(BotRole role);
}

#endif
