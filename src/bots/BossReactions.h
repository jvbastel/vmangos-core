#ifndef WOWAI_BOTS_BOSS_REACTIONS_H
#define WOWAI_BOTS_BOSS_REACTIONS_H

#include "BotRecipe.h"
#include "Strategy.h"

#include <cstdint>
#include <memory>
#include <string>

// The boss reaction registry (spec 7, Boss reaction registry). A boss
// reaction is a strategy of the Boss band, keyed by the creature entry of
// the boss. Each boss reaction is in its own file (for example
// TaragamanReaction.cpp). To add one: a row in the table of
// BossReactions.cpp.
//
// When the group enters combat with a boss that has a reaction, the group
// coordinator holds the boss fight, and each bot pushes the reaction onto its
// stack. After the fight, each bot pops it (BossReactionSlot).
namespace Bots
{
    // True when the creature entry `entry` has a boss reaction.
    bool HasBossReaction(std::uint32_t entry);

    // The boss reaction of creature entry `entry` for a bot of role `role`,
    // or null when the entry has none. All roles get a strategy of the same
    // name; a role with nothing to do gets one with no rules.
    std::shared_ptr<Strategy const> MakeBossReaction(std::uint32_t entry, BotRole role);

    // The boss reaction on the stack of one bot. BotAI updates it before
    // each think.
    class BossReactionSlot
    {
    public:
        enum class Change
        {
            None,
            Pushed,
            Popped,
        };

        // 1. Starts the boss fight in the coordinator when a boss with a
        //    reaction fights a member of the group: it attacks a member, or
        //    a member attacks it while it is in combat.
        // 2. Ends the boss fight when the bot sees the boss dead or out of
        //    combat (a wipe). A bot that cannot see the boss keeps the fight.
        // 3. Pushes the reaction of the boss fight onto `stack`, or pops it
        //    when the fight ended.
        Change Update(BotView const& view, GroupCoordinator& group, StrategyStack& stack, BotRole role);

        // The name of the reaction on the stack, or "".
        std::string const& Active() const { return m_active; }

    private:
        std::string m_active;
        Guid m_boss = NoGuid;
    };
}

#endif
