#include "PlayerTank.h"
#include "CommonRules.h"

namespace Bots
{
    bool IsInCombatWithTheGroup(BotView const& view, Guid unit)
    {
        if (!IsAlive(view, unit) || IsGroupMember(view, unit) || !view.IsInCombat(unit))
            return false;
        if (IsGroupMember(view, view.GetVictim(unit)))
            return true;
        for (Guid member : view.GetGroupMembers())
            if (view.GetThreatRatio(member, unit) > 0.0f)
                return true;
        return false;
    }

    bool IsTopOfThreat(BotView const& view, Guid tank, Guid enemy)
    {
        return view.GetThreatRatio(tank, enemy) >= 1.0f;
    }

    void WatchPlayerTank(BotView const& view, GroupCoordinator& group, std::int64_t nowMs)
    {
        if (!group.IsPlayerTankGroup())
            return;
        Guid const tank = group.GetTank();
        // jvbastel/WOWAI#195: the tank died mid-fight: the focus stays as
        // it is, whatever the dead tank still targets.
        if (SeenDead(view, tank))
            return;
        Guid const target = view.GetTargetOf(tank);
        if (target != NoGuid && IsInCombatWithTheGroup(view, target))
            group.SetFocusTarget(target);
        Guid const focus = group.GetFocusTarget();
        // A bot that does not see the tank or the focus has no threat to
        // read: it notes nothing, so it does not start the clock again.
        if (focus != NoGuid && (!IsAlive(view, tank) || !IsAlive(view, focus)))
            return;
        group.NoteTankOnTop(focus != NoGuid && IsTopOfThreat(view, tank, focus), nowMs);
    }
}
