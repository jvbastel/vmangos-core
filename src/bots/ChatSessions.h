#ifndef WOWAI_BOTS_CHAT_SESSIONS_H
#define WOWAI_BOTS_CHAT_SESSIONS_H

#include "ChatContract.h"
#include "GroupCoordinator.h"
#include <string>

#include <cstdint>
#include <map>
#include <vector>

// The chat sessions of the bots: the time that a bot spends in one group with
// the player. The bot manager gives a view of the groups in each update, and
// gets a session_end event when a bot left, the group disbanded, or the player
// logged out. So it needs no core hook for these.
namespace Bots
{
    // A group as bot chat sees it, at one moment.
    struct ChatGroupView
    {
        std::uint32_t groupId = 0;
        // The player of the group. A guid of 0: the group has no player.
        ChatPlayer player;
        bool playerOnline = false;
        // The bots of the group that are online.
        std::vector<ChatEventBot> bots;
        std::string zone; // The zone of the player.
    };

    // An event with the facts of the group.
    ChatEvent MakeChatEvent(ChatGroupView const& group, ChatEventKind kind);

    // The chat event of a group event of the coordinator: `boss_kill`,
    // `wipe`, `loot_roll`, or `rest`. `name` is the fact that the game
    // knows: the boss name of a boss kill, or the item name of a loot roll.
    // False when the group has no chat session (no player online, or no bot),
    // or when the bot of a rest is not in the group.
    bool MakeGroupChatEvent(ChatGroupView const& group, GroupEvent const& event, std::string const& name, ChatEvent& chatEvent);

    class ChatSessions
    {
    public:
        // `groups`: the groups with a bot of ours, and each group of
        // Groups() that still exists. Gives the session_end events.
        std::vector<ChatEvent> Update(std::vector<ChatGroupView> const& groups);

        // The groups with a chat session.
        std::vector<std::uint32_t> Groups() const;

    private:
        // The last view of each group with a chat session.
        std::map<std::uint32_t, ChatGroupView> m_sessions;
    };
}

#endif
