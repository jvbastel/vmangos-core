#include "CommandState.h"

namespace Bots
{
    namespace
    {
        char const* const DeadLine = "I am dead.";
        char const* const OutOfReachLine = "I cannot reach you.";

        CommandOutcome Obey(CommandState state)
        {
            return {true, state, ""};
        }

        CommandOutcome Refuse(CommandState current, char const* line)
        {
            return {false, current, line};
        }
    }

    CommandOutcome ApplyBotCommand(CommandState current, BotCommandKind kind, CommandFacts const& facts)
    {
        switch (kind)
        {
            case BotCommandKind::Stop:
                return Obey(CommandState::None);
            case BotCommandKind::Stay:
                if (!facts.botAlive)
                    return Refuse(current, DeadLine);
                return Obey(CommandState::Stay);
            case BotCommandKind::Follow:
            case BotCommandKind::Come:
                if (!facts.botAlive)
                    return Refuse(current, DeadLine);
                if (!facts.playerInReach)
                    return Refuse(current, OutOfReachLine);
                return Obey(kind == BotCommandKind::Follow ? CommandState::Follow : CommandState::Come);
        }
        return Obey(current);
    }
}
