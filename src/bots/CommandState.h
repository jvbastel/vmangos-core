#ifndef WOWAI_BOTS_COMMAND_STATE_H
#define WOWAI_BOTS_COMMAND_STATE_H

#include "BotCommand.h"

// The command state of a bot: what the last bot command told it to do. It
// stays until a new bot command changes it.
namespace Bots
{
    enum class CommandState
    {
        None,   // No bot command holds the bot.
        Follow, // The bot follows the player.
        Stay,   // The bot stays where it is.
        Come,   // The bot goes to where the player was, then stays there.
    };

    // The target of the player who gave the command, for `pull`.
    enum class PullTarget
    {
        None,     // The player has no target.
        Enemy,    // An enemy that the bot can attack.
        NotEnemy, // A friend, a dead unit, or a unit that the bot cannot attack.
    };

    // What the game layer knows about the bot when a bot command arrives.
    struct CommandFacts
    {
        bool botAlive;
        // The player who gave the command is on the map of the bot and near
        // enough to walk to.
        bool playerInReach;
        // The bot is a tank: only a tank pulls.
        bool isTank;
        PullTarget playerTarget;
        // A bot of the group eats or drinks after a fight: the group waits,
        // so the tank does not pull.
        bool groupRests = false;
    };

    struct CommandOutcome
    {
        bool obeys;
        CommandState state; // The new command state. Unchanged when the bot does not obey.
        // A short fixed line that the bot says when it does not obey, else "".
        char const* line;
    };

    // The new command state of a bot after a bot command. A bot obeys
    // silently; only a bot that cannot obey gets a line. `pull` keeps the
    // command state: the tank pulls, and then follows again. A bot that is
    // no tank ignores `pull` without a line.
    CommandOutcome ApplyBotCommand(CommandState current, BotCommandKind kind, CommandFacts const& facts);
}

#endif
