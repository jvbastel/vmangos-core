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

    // jvbastel/WOWAI#191: the focus order. A mark counts only on an enemy in
    // combat with the group, so a mark never makes a bot pull.
    Guid PickPlayerTankFocus(BotView const& view, Guid tank)
    {
        BotView::GroupMarks const marks = view.GetGroupMarks();
        for (Guid candidate : {marks.skull, marks.cross, view.GetTargetOf(tank)})
            if (candidate != NoGuid && IsInCombatWithTheGroup(view, candidate))
                return candidate;
        return NoGuid;
    }

    void WatchPlayerTank(BotView const& view, GroupCoordinator& group, std::int64_t nowMs)
    {
        if (!group.IsPlayerTankGroup())
            return;
        // Spec #188 fix-up (S2): the group left combat, and the focus is
        // out of the fight: the fight is over. No focus, and no tank on top.
        // Only a bot that sees the focus (dead or alive) judges it.
        Guid const last = group.GetFocusTarget();
        if (last != NoGuid && view.GetHealth(last).max > 0 && !IsInCombatWithTheGroup(view, last)
            && !IsGroupInCombat(view))
            group.SetFocusTarget(NoGuid);
        Guid const tank = group.GetTank();
        // jvbastel/WOWAI#195: the tank died mid-fight: the focus stays as
        // it is, whatever the dead tank still targets or marks.
        if (SeenDead(view, tank))
            return;
        Guid const target = PickPlayerTankFocus(view, tank);
        if (target != NoGuid)
            group.SetFocusTarget(target);
        Guid const focus = group.GetFocusTarget();
        // A bot that does not see the tank or the focus has no threat to
        // read: it notes nothing, so it does not start the clock again.
        if (focus != NoGuid && (!IsAlive(view, tank) || !IsAlive(view, focus)))
            return;
        group.NoteTankOnTop(focus != NoGuid && IsTopOfThreat(view, tank, focus), nowMs);
    }
}
