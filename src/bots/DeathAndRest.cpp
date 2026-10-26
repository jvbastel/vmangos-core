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

        // True while a resurrect can still come to the dead bot: a living
        // member that it sees can resurrect, and has not given up its
        // resurrect (no safe place to cast it from).
        bool ResurrectCanCome(BotContext const& context)
        {
            if (context.group.IsResurrectGivenUp(context.view.GetSelf()))
                return false;
            for (Guid member : context.view.GetGroupMembers())
                if (context.view.CanResurrect(member))
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

        // Why the bot rests now: its meal still fills it. Eating comes first.
        RestReason RestReasonOf(BotView const& view)
        {
            Guid const self = view.GetSelf();
            if (!IsAlive(view, self))
                return RestReason::None;
            Health const health = view.GetHealth(self);
            Power const mana = view.GetMana(self);
            if (view.IsEating() && health.current < health.max)
                return RestReason::Eat;
            if (view.IsDrinking() && mana.current < mana.max)
                return RestReason::Drink;
            return RestReason::None;
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

            // Not in a wipe: then the bots wait for the leader to release.
            // After the fight, so that a Priest that lives can still come.
            Rule{"release when no resurrect comes", P + 20,
                [](BotContext const& context) {
                    return SelfState(context) == LifeState::Dead && SomeoneLives(context.view)
                        && !IsGroupInCombat(context.view) && !ResurrectCanCome(context);
                },
                Always([](BotContext const& context) {
                    context.actions.Release();
                    context.group.NoteReleaseToRunBack(context.view.GetSelf());
                })},

            // No bot revives at its corpse on its own: a ghost runs back.
            Rule{"walk back as a ghost", P + 10,
                [](BotContext const& context) { return SelfState(context) == LifeState::Ghost; },
                Always([](BotContext const& context) { context.actions.WalkAsGhost(); })},

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
                [](BotContext const& context) { return AfterFight(context) && RestsNow(context.view); },
                Always(Nothing)},
        };
        return std::make_shared<Strategy const>(Strategy{"rest", std::move(rules)});
    }

    void WatchGroup(BotView const& view, GroupCoordinator& group)
    {
        group.SetWiped(!SomeoneLives(view));
        if (IsAlive(view, view.GetSelf()))
            group.ClearGivenUpResurrect(view.GetSelf());
        // The bot that resurrects judges the blockers: it stands where they
        // are. A bot far away, or a ghost, would see each of them as dead.
        if (view.CanResurrect(view.GetSelf()))
            group.RetryResurrects([&view](Guid enemy) { return IsAlive(view, enemy); });

        group.SetResting(view.GetSelf(), RestReasonOf(view));
    }

    bool RestsNow(BotView const& view)
    {
        return RestReasonOf(view) != RestReason::None;
    }
}
