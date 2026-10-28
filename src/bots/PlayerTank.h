#ifndef WOWAI_BOTS_PLAYER_TANK_H
#define WOWAI_BOTS_PLAYER_TANK_H

#include "BotTypes.h"
#include "BotView.h"
#include "GroupCoordinator.h"

#include <cstdint>

// jvbastel/WOWAI#189 (spec #188): the player tank. A human leads a group of
// bots with no bot tank (GroupCoordinator::IsPlayerTankGroup). No bot pulls:
// the human starts each fight, and the bots take their focus from the
// human's target (BotView::GetTargetOf).
namespace Bots
{
    // True when enemy `unit` is in combat with the bot's group: it lives,
    // is no group member, fights, and it hits a group member or a group
    // member has threat on it. A friend has no threat list, so a friend is
    // never in combat with the group.
    bool IsInCombatWithTheGroup(BotView const& view, Guid unit);

    // True when the tank `tank` is top of the threat list of enemy `enemy`:
    // no other group member has more threat on it (BotView::GetThreatRatio).
    bool IsTopOfThreat(BotView const& view, Guid tank, Guid enemy);

    // The player-tank watch of one bot, before each think. Nothing in a
    // group that is no player-tank group. Else:
    // - The focus becomes the tank's target when that target is an enemy in
    //   combat with the group (IsInCombatWithTheGroup). A friend, no target,
    //   or an enemy out of the fight keeps the last focus.
    // - The bot notes at `nowMs` whether the tank is top of the focus's
    //   threat list (GroupCoordinator::NoteTankOnTop): the tank holds threat
    //   PlayerTankHoldMs after it got on top (TankHoldsThreat). A bot that
    //   does not see the tank or the focus alive notes nothing.
    void WatchPlayerTank(BotView const& view, GroupCoordinator& group, std::int64_t nowMs);
}

#endif
