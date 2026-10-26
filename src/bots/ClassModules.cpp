#include "ClassModules.h"
#include "CommonRules.h"
#include "DeathAndRest.h"
#include "FollowTank.h"
#include "GroupRules.h"
#include "MageModule.h"
#include "PriestModule.h"
#include "RogueModule.h"
#include "WarlockModule.h"
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
            {BotClass::Priest, BotRole::Healer, &Priest::MakeHealerStrategy},
            {BotClass::Rogue, BotRole::Melee, &Rogue::MakeMeleeStrategy},
            {BotClass::Warlock, BotRole::Ranged, &Warlock::MakeRangedStrategy},
        };

        bool ShouldFollow(BotContext const& context)
        {
            Guid const self = context.view.GetSelf();
            return context.command == CommandState::Follow
                && !context.startWaits
                && !context.view.IsInCombat(self)
                // The tank on its way to the pull target.
                && context.group.GetPull().puller != self
                // A damage bot on its way to its target.
                && !IsAlive(context.view, context.view.GetChase().target)
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
        // Over the loot and the follow rule: while the tank pulls, the bot
        // keeps up with it (FollowTank.h).
        Rule const followTank = FollowTankRule(Priority::Command + 25);
        return std::make_shared<Strategy const>(Strategy{"command", {followTank, loot, follow}});
    }

    std::shared_ptr<Strategy const> MakeClassStrategy(BotClass botClass, BotRole role)
    {
        for (ClassModule const& module : Modules)
            if (module.botClass == botClass && module.role == role)
                return module.make();
        return nullptr;
    }

    std::vector<SpellId> PetAutocastSpells(BotClass botClass)
    {
        if (botClass == BotClass::Warlock)
            return {Warlock::Firebolt, Warlock::BloodPact};
        return {};
    }

    StrategyStack MakeStrategyStack(BotClass botClass, BotRole role)
    {
        StrategyStack stack;
        stack.Push(MakeCommandStrategy());
        stack.Push(MakeRestStrategy());
        stack.Push(MakeGroupStrategy(role));
        if (std::shared_ptr<Strategy const> strategy = MakeClassStrategy(botClass, role))
            stack.Push(strategy);
        stack.Push(MakeDeathStrategy());
        return stack;
    }
}
