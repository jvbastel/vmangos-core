#ifndef WOWAI_BOTS_GROUP_COMMAND_H
#define WOWAI_BOTS_GROUP_COMMAND_H

#include <string>

// The bot commands that bring a bot into the group of the player and send it
// away: `invite <name>` and `dismiss <name>`. The bot manager handles them,
// not the inbox of a bot, because the bot can be offline or in no group.
namespace Bots
{
    enum class GroupCommandKind
    {
        Invite,  // Log the bot in if needed, add it to the group, teleport it to the player.
        Dismiss, // The bot leaves the group and logs out.
    };

    struct GroupCommand
    {
        GroupCommandKind kind;
        // The name as the player wrote it. Empty: the command word alone,
        // which in a whisper is for the whispered bot.
        std::string botName;
    };

    struct ParsedGroupCommand
    {
        bool ok;              // False: the text is no group command.
        GroupCommand command; // Only when ok.
    };

    // Reads `<command word> [bot name]`. The word ignores case, and spaces
    // around the words do not count. The name can be any one word: the bot
    // manager knows the bots, also the offline ones.
    ParsedGroupCommand ParseGroupCommand(std::string const& text);

    // What the bot manager knows when a group command arrives.
    struct GroupFacts
    {
        bool botKnown;         // A bot has the name.
        bool botOnline;        // The bot is in the world.
        bool botInPlayerGroup; // The bot is a member of the group of the player.
        bool botInOtherGroup;  // The bot is a member of another group.
        bool playerMayInvite;  // The player has no group, or leads it or assists.
        bool groupFull;        // The group of the player has no free place.
        // jvbastel/WOWAI#199.
        bool botOfOtherFaction; // The bot is not of the player's faction.
        // jvbastel/WOWAI#197: the bot has the tank role, and the group of
        // the player refuses a bot tank (GroupTank.h, RefusesBotTank).
        bool secondTank = false;
    };

    // jvbastel/WOWAI#197: the line of a bot tank that the player who picked
    // tank invites. The bot says it in party chat, to the player.
    char const* const SecondTankRefusalLine = "You tank this group, so I stay out of it.";

    // The steps of a group command, in this order. An offline bot only logs
    // in: the bot manager plans again when the bot is in the world.
    struct GroupCommandPlan
    {
        bool ok;
        // A short fixed line for the player when not ok, else "".
        char const* line;
        bool login;
        bool addToGroup;
        bool teleport; // To the player.
        bool leaveGroup;
        bool logout;
        // jvbastel/WOWAI#197: the bot says `line` in party chat, to the
        // player. Else the server tells it to the player.
        bool lineFromBot = false;
    };

    GroupCommandPlan PlanGroupCommand(GroupCommandKind kind, GroupFacts const& facts);
}

#endif
