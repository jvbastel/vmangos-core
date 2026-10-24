#include "DeathAndRest.h"
#include "CommonRules.h"

namespace Bots
{
    namespace
    {
        LifeState SelfState(BotContext const& context)
        {
            return context.view.GetLifeState(context.view.GetSelf());
        }

        bool IsDead(BotContext const& context)
        {
            return !IsAlive(context.view, context.view.GetSelf());
        }

        // True when a living member that the bot sees can resurrect.
        bool SomeoneCanResurrect(BotView const& view)
        {
            for (Guid member : view.GetGroupMembers())
                if (view.CanResurrect(member))
                    return true;
            return false;
        }

        bool SomeoneLives(BotView const& view)
        {
            for (Guid member : view.GetGroupMembers())
                if (IsAlive(view, member))
                    return true;
            return false;
        }

        bool Below(std::uint32_t current, std::uint32_t max)
        {
            return max > 0 && current * 2 < max;
        }

        // After a fight: the bot is alive and nobody of its group fights.
        // The puller is on its way to a fight.
        bool AfterFight(BotContext const& context)
        {
            Guid const self = context.view.GetSelf();
            return IsAlive(context.view, self) && !IsGroupInCombat(context.view)
                && context.group.GetPull().puller != self;
        }

        BotAction Always(std::function<void(BotContext const&)> run)
        {
            return BotAction{[](BotContext const&) { return true; }, std::move(run)};
        }

        void Nothing(BotContext const&) {}
    }

    std::shared_ptr<Strategy const> MakeDeathStrategy()
    {
        int const P = Priority::Death;
        std::vector<Rule> rules = {
            Rule{"release with the leader", P + 30,
                [](BotContext const& context) {
                    Guid const leader = context.view.GetGroupLeader();
                    return SelfState(context) == LifeState::Dead && leader != NoGuid
                        && leader != context.view.GetSelf()
                        && context.view.GetLifeState(leader) == LifeState::Ghost;
                },
                Always([](BotContext const& context) { context.actions.Release(); })},

            Rule{"revive at the corpse", P + 20,
                [](BotContext const& context) {
                    Guid const leader = context.view.GetGroupLeader();
                    return SelfState(context) == LifeState::Ghost && leader != NoGuid
                        && context.view.GetLifeState(leader) == LifeState::Alive
                        && context.view.GetCorpseDistance(leader) <= ReviveNearLeaderYards;
                },
                Always([](BotContext const& context) {
                    context.actions.ReviveAtCorpse();
                    context.group.NoteRevive(context.view.GetSelf());
                })},

            // Not in a wipe: then the bots wait for the leader to release.
            Rule{"revive when nobody can resurrect", P + 10,
                [](BotContext const& context) {
                    return SelfState(context) == LifeState::Dead && SomeoneLives(context.view)
                        && !IsGroupInCombat(context.view) && !SomeoneCanResurrect(context.view);
                },
                Always([](BotContext const& context) {
                    context.actions.ReviveAtCorpse();
                    context.group.NoteRevive(context.view.GetSelf());
                })},

            // A dead bot does nothing else.
            Rule{"wait dead", P, IsDead, Always(Nothing)},
        };
        return std::make_shared<Strategy const>(Strategy{"death", std::move(rules)});
    }

    std::shared_ptr<Strategy const> MakeRestStrategy()
    {
        int const P = Priority::Command;
        std::vector<Rule> rules = {
            Rule{"eat", P + 32,
                [](BotContext const& context) {
                    Health const health = context.view.GetHealth(context.view.GetSelf());
                    return AfterFight(context) && !context.view.IsEating() && Below(health.current, health.max);
                },
                Always([](BotContext const& context) { context.actions.Eat(); })},
            Rule{"drink", P + 31,
                [](BotContext const& context) {
                    Power const mana = context.view.GetMana(context.view.GetSelf());
                    return AfterFight(context) && !context.view.IsDrinking() && Below(mana.current, mana.max);
                },
                Always([](BotContext const& context) { context.actions.Drink(); })},
            // Over the corpse loot and the follow rule: a move stands the
            // bot up and ends its meal.
            Rule{"keep resting", P + 30,
                [](BotContext const& context) {
                    Guid const self = context.view.GetSelf();
                    Health const health = context.view.GetHealth(self);
                    Power const mana = context.view.GetMana(self);
                    return AfterFight(context)
                        && ((context.view.IsEating() && health.current < health.max)
                            || (context.view.IsDrinking() && mana.current < mana.max));
                },
                Always(Nothing)},
        };
        return std::make_shared<Strategy const>(Strategy{"rest", std::move(rules)});
    }

    void WatchGroup(BotView const& view, GroupCoordinator& group)
    {
        group.SetWiped(!SomeoneLives(view));

        RestReason reason = RestReason::None;
        if (view.IsEating())
            reason = RestReason::Eat;
        else if (view.IsDrinking())
            reason = RestReason::Drink;
        group.SetResting(view.GetSelf(), IsAlive(view, view.GetSelf()) ? reason : RestReason::None);
    }
}
