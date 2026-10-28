#include "BossFights.h"
#include "CommonRules.h"

#include <vector>

namespace Bots
{
    namespace
    {
        // The bot sees unit `unit` dead. False for a unit that it cannot see.
        bool SeesDead(BotView const& view, Guid unit)
        {
            Health const health = view.GetHealth(unit);
            return health.max != 0 && health.current == 0;
        }
    }

    Guid FindBossInFight(BotView const& view, std::function<bool(Guid)> const& isBoss)
    {
        for (Guid member : view.GetGroupMembers())
        {
            std::vector<Guid> units = view.GetAttackers(member);
            units.push_back(view.GetVictim(member));
            for (Guid unit : units)
                if (unit != NoGuid && isBoss(unit) && IsAlive(view, unit) && view.IsInCombat(unit))
                    return unit;
        }
        return NoGuid;
    }

    void WatchBossFought(BotView const& view, GroupCoordinator& group, bool someoneLives)
    {
        BossFight const fought = group.GetBossFought();
        if (fought.boss != NoGuid)
        {
            bool const seen = view.GetHealth(fought.boss).max != 0;
            if (!SeesDead(view, fought.boss) && !(someoneLives && seen && !view.IsInCombat(fought.boss)))
                return;
            // Another bot of the group may have noted a new boss since.
            if (!group.ReplaceBossFought(fought, {NoGuid, 0}))
                return;
        }
        Guid const boss = FindBossInFight(view, [&view](Guid unit) { return view.IsListedBoss(unit); });
        if (boss != NoGuid)
            group.ReplaceBossFought({NoGuid, 0}, {boss, view.GetEntry(boss)});
    }

    void TapWatch::Update(BotView const& view, GroupCoordinator& group, std::int64_t nowMs)
    {
        if (m_boss == NoGuid)
        {
            m_boss = FindBossInFight(view, [&view](Guid unit) { return view.IsListedBoss(unit) && view.HoldsTap(unit); });
            return;
        }
        if (IsAlive(view, m_boss) && view.IsInCombat(m_boss))
            return;
        // Dead: the kill. Alive out of combat (an evade, a wipe), or out of
        // sight: no kill.
        if (SeesDead(view, m_boss) && view.HoldsTap(m_boss))
            group.NoteBossKill(m_boss, view.GetEntry(m_boss), nowMs);
        m_boss = NoGuid;
    }
}
