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
}

#endif
