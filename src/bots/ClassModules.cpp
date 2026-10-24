#include "ClassModules.h"
#include "CommonRules.h"
#include "MageModule.h"
#include "WarriorModule.h"

namespace Bots
{
    namespace
    {
        // A following bot follows the player within this distance.
        float const FollowReachYards = 100.0f;

        struct ClassModule
        {
            BotClass botClass;
            BotRole role;
            std::shared_ptr<Strategy const> (*make)();
        };

        ClassModule const Modules[] = {
            {BotClass::Warrior, BotRole::Tank, &Warrior::MakeTankStrategy},
            {BotClass::Mage, BotRole::Ranged, &Mage::MakeFrostStrategy},
        };

        bool ShouldFollow(BotContext const& context)
        {
            Guid const self = context.view.GetSelf();
            return context.command == CommandState::Follow
                && !context.view.IsInCombat(self)
                // The tank on its way to the pull target.
                && context.group.GetPull().puller != self
                && context.view.GetFollowTarget() != context.commandPlayer;
        }

        // A bot may walk to a corpse when it follows or has no command. A
        // bot that stays or comes loots only a corpse in reach.
        bool MayWalkToLoot(BotContext const& context)
        {
            return context.command == CommandState::Follow || context.command == CommandState::None;
        }

        bool ShouldLoot(BotContext const& context)
        {
            Guid const self = context.view.GetSelf();
            return !context.view.IsInCombat(self) && context.group.GetPull().puller != self;
        }
    }

    std::shared_ptr<Strategy const> MakeCommandStrategy()
    {
        Rule follow;
        follow.name = "follow the player";
        follow.priority = Priority::Command + 10;
        follow.trigger = ShouldFollow;
        follow.action.canDo = [](BotContext const& context) {
            return IsAlive(context.view, context.commandPlayer)
                && context.view.GetDistance(context.commandPlayer) <= FollowReachYards;
        };
        follow.action.run = [](BotContext const& context) { context.actions.Follow(context.commandPlayer); };

        // Over the follow rule: a looting bot finishes the corpse, and then
        // follows again.
        Rule loot;
        loot.name = "loot a corpse";
        loot.priority = Priority::Command + 20;
        loot.trigger = ShouldLoot;
        loot.action.canDo = [](BotContext const& context) { return context.view.HasLootWork(MayWalkToLoot(context)); };
        loot.action.run = [](BotContext const& context) { context.actions.Loot(MayWalkToLoot(context)); };
        return std::make_shared<Strategy const>(Strategy{"command", {loot, follow}});
    }

    std::shared_ptr<Strategy const> MakeClassStrategy(BotClass botClass, BotRole role)
    {
        for (ClassModule const& module : Modules)
            if (module.botClass == botClass && module.role == role)
                return module.make();
        return nullptr;
    }

    StrategyStack MakeStrategyStack(BotClass botClass, BotRole role)
    {
        StrategyStack stack;
        stack.Push(MakeCommandStrategy());
        if (std::shared_ptr<Strategy const> strategy = MakeClassStrategy(botClass, role))
            stack.Push(strategy);
        return stack;
    }
}
