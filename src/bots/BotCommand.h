#ifndef WOWAI_BOTS_BOT_COMMAND_H
#define WOWAI_BOTS_BOT_COMMAND_H

#include <string>
#include <vector>

// The bot commands that the player types in party chat or in a whisper.
// The parser is a pure function: the game layer routes the parsed command to
// the inboxes of the addressed bots.
namespace Bots
{
    // To add a bot command: add a kind here and its word to the table of
    // BotCommand.cpp.
    enum class BotCommandKind
    {
        Follow, // The bot follows the player.
        Stay,   // The bot stays where it is.
        Come,   // The bot comes to the player.
        Stop,   // The bot stops its current bot action.
        Pull,   // The tank pulls the target of the player.
    };

    struct BotCommand
    {
        BotCommandKind kind;
        // The bot that the bot name at the start addresses, as written in the
        // list of bot names. Empty: the command is for all bots.
        std::string botName;
    };

    struct ParsedBotCommand
    {
        bool ok;            // False: the text is no bot command.
        BotCommand command; // Only when ok.
    };

    // Reads `[bot name] <command word>`. The words ignore case, and spaces
    // around them do not count. A bot name must be one of `botNames`, the
    // bots of the group (or the whispered bot). Any other text is no bot
    // command, so that it can go to the chat service.
    ParsedBotCommand ParseBotCommand(std::string const& text, std::vector<std::string> const& botNames);

    // The names in `botNames` that a parsed command addresses: all of them,
    // or only the bot of the bot name at the start.
    std::vector<std::string> AddressedBots(BotCommand const& command, std::vector<std::string> const& botNames);

    // The word of a bot command, for example "follow".
    char const* BotCommandWord(BotCommandKind kind);
}

#endif
