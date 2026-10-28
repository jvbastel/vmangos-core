#ifndef WOWAI_BOTS_SAFE_PLACE_H
#define WOWAI_BOTS_SAFE_PLACE_H

#include "BotView.h"

#include <functional>
#include <vector>

// Safe places to move to: the one rule for a bot that leaves its spot on
// its own, such as the Priest's walk to a corpse (WOWAI#72) and the Fire
// Nova move at Taragaman (WOWAI#74). A place is safe when:
// - the bot has a ground path to it (BotView::FindGroundPath),
// - no step of that path, nor the place, is on harmful ground such as lava
//   (BotView::IsHarmfulGround, checked each yard of the path: a path that
//   is no navmesh path is never taken, and the grid liquid has the last
//   word on lava),
// - each unit to see is in sight from the place, and
// - with enemies to keep away from, the place keeps out of each enemy's
//   aggro distance and a margin, and so does the path, except for a step
//   that goes no nearer to the enemy than the bot stands now. Both are
//   measured from the enemy's center, as the game measures aggro: the
//   bodies of the bot and the enemy do not count (the margin covers them).
namespace Bots
{
    // Yards more than the aggro distance of an enemy, for a place and a
    // path that must pull it in no case.
    float const AggroMarginYards = 5.0f;
    // The length of a step of the path check.
    float const SafePathStepYards = 1.0f;

    struct SafeMoveRules
    {
        // The units to see from the place. None: no sight check.
        std::vector<Guid> inSightOf;
        // The places to see from the place (BotView::CanSeeBetween), such as
        // a fight spot that the tank has not reached yet.
        std::vector<Position> inSightOfPlaces;
        // The enemies not to pull. None: no pull check.
        std::vector<NearEnemy> enemies;
    };

    // A bot at `at` pulls one of `enemies`: it stands within the aggro
    // distance and AggroMarginYards of the enemy (from its center). The
    // place rule of FirstSafePlace, also for the fight spot (FightSpot.h).
    bool PullsAnEnemy(std::vector<NearEnemy> const& enemies, Position const& at);

    // The first of `spots`, in their order, that is safe for the bot to
    // move to. False when none is.
    bool FirstSafePlace(BotView const& view, std::vector<Position> const& spots, SafeMoveRules const& rules,
        Position& place);
    // The same for a unit that stands at `from`, such as another member.
    bool FirstSafePlaceFrom(BotView const& view, Position const& from, std::vector<Position> const& spots,
        SafeMoveRules const& rules, Position& place);

    // The cast place for a spell on unit `unit`, for example the resurrect
    // of a corpse: within `yards` of it, in its sight, reached on a path
    // that pulls no enemy around the bot and the unit. The spots are on three
    // rings round the unit (0.9, 0.6 and 0.3 of `yards`), on the ground and
    // at most 8 yards over or under it, and the nearest to the bot counts
    // first. False when no spot is safe, and for a unit too far away to know
    // the enemies on the way (the bot looks 150 yards around it, so about
    // 84 yards for a cast of 26 yards). `enemies`, when given: the enemies
    // that the search kept away from. `accepts`, when given: the caller's
    // own rule for a place, such as the shot range of Ranged.h (a spell with
    // a least range). jvbastel/WOWAI#195: `keepOutOf`, when given: the
    // enemies to keep away from in place of those near the bot, such as the
    // packs outside the fight (KeepOut.h) for a move in a fight.
    bool FindSafeCastPlace(BotView const& view, Guid unit, float yards, Position& place,
        std::vector<NearEnemy>* enemies = nullptr, std::function<bool(Position const&)> const& accepts = nullptr,
        std::vector<NearEnemy> const* keepOutOf = nullptr);
}

#endif
