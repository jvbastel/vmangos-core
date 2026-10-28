#ifndef WOWAI_BOTS_BOSS_FIGHTS_H
#define WOWAI_BOTS_BOSS_FIGHTS_H

#include "BotView.h"
#include "GroupCoordinator.h"

#include <cstdint>
#include <functional>

// The boss fights of a group, for the bot chat events (spec 8, Events from
// packets and the coordinator): the boss that a wipe names, and the boss
// kill of the member that holds the tap.
namespace Bots
{
    // A living boss in combat that fights a member of the group: it attacks
    // a member, or a member attacks it. `isBoss` picks the bosses. NoGuid
    // when none fights.
    Guid FindBossInFight(BotView const& view, std::function<bool(Guid)> const& isBoss);

    // Notes the boss of the boss list (BotView::IsListedBoss) that fights
    // the group in the coordinator (GroupCoordinator::GetBossFought), for
    // the boss name of a wipe. The boss stays while it lives in combat, and
    // through a wipe: the boss evades before the bots see that all are dead.
    // It ends when the bot sees it dead, or out of combat while
    // `someoneLives`. A bot that cannot see the boss keeps it.
    void WatchBossFought(BotView const& view, GroupCoordinator& group, bool someoneLives);

    // The boss kill of the bot that holds the tap. Unit::Kill sends
    // SMSG_PARTYKILLLOG to each member but the loot recipient, so in a group
    // where that bot is the only bot, no bot gets the packet. The watch
    // remembers a boss of the boss list that the bot holds the tap on
    // (BotView::HoldsTap) in a fight, and notes its kill when the bot sees
    // it dead. The coordinator keeps one kill when the packet comes too.
    class TapWatch
    {
    public:
        void Update(BotView const& view, GroupCoordinator& group, std::int64_t nowMs);

    private:
        Guid m_boss = NoGuid;
    };
}

#endif
