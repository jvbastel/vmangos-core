#ifndef WOWAI_BOTS_STRATEGY_H
#define WOWAI_BOTS_STRATEGY_H

#include "BotActions.h"
#include "BotTypes.h"
#include "BotView.h"
#include "CommandState.h"
#include "GroupCoordinator.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

// The strategy engine. A strategy is a named set of rules, and each rule is
// a trigger, a bot action, and a priority. Each bot has a stack of active
// strategies: its command state, the generic group rules, its class and role
// strategy, and any active boss reaction. On each think, the bot does the
// highest-priority bot action whose trigger fires and which it can do now.
namespace Bots
{
    // What a rule sees and uses on one think of one bot.
    struct BotContext
    {
        BotView const& view;
        BotActions& actions;
        // The coordinator of the bot's group, or the bot's own one.
        GroupCoordinator& group;
        // The command state of the bot, and the player of that command.
        CommandState command;
        Guid commandPlayer;
    };

    using Trigger = std::function<bool(BotContext const&)>;

    struct BotAction
    {
        // True when the bot can do the action now: for a spell, the bot
        // knows it, has the power, is in range, and the spell is ready.
        std::function<bool(BotContext const&)> canDo;
        // Does the action through context.actions, or writes the group
        // coordinator.
        std::function<void(BotContext const&)> run;
    };

    struct Rule
    {
        std::string name;
        // Higher first. See the bands in `Priority`.
        int priority;
        Trigger trigger;
        BotAction action;
    };

    struct Strategy
    {
        std::string name;
        std::vector<Rule> rules;
    };

    // The priority bands of the strategies. A rule adds 0 to 99 to the band
    // of its strategy, so that the bands do not overlap.
    namespace Priority
    {
        int const Command = 100; // The command state, for example follow.
        int const Class = 200;   // The class and role strategy.
        int const Group = 300;   // The generic group rules, for example interrupts.
        int const Boss = 400;    // A boss reaction.
        int const Death = 500;   // A dead bot: release and revive, and nothing else.
    }

    // The active strategies of one bot, bottom first.
    class StrategyStack
    {
    public:
        void Push(std::shared_ptr<Strategy const> strategy);
        // Removes the strategy named `name`. False if it is not active.
        bool Remove(std::string const& name);
        bool Contains(std::string const& name) const;
        // The names of the active strategies, bottom first.
        std::vector<std::string> Names() const;

        // Does the highest-priority bot action whose trigger fires and which
        // the bot can do now, and returns the name of its rule. "" when the
        // bot does nothing. On a tie, the strategy higher on the stack wins,
        // and in one strategy the first rule wins.
        std::string Think(BotContext const& context) const;

    private:
        std::vector<std::shared_ptr<Strategy const>> m_strategies;
    };
}

#endif
