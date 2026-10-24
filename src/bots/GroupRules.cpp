#include "GroupRules.h"
#include "CommonRules.h"
#include "RogueModule.h"
#include "SpellBook.h"
#include "WarriorModule.h"

#include <algorithm>

namespace Bots
{
    namespace
    {
        // The interrupt spells, by the spell ID of the first rank. Shield
        // Bash needs a shield: BotView::CanCast checks it.
        SpellId const InterruptSpells[] = {Rogue::Kick, Warrior::ShieldBash};

        Guid Self(BotContext const& context)
        {
            return context.view.GetSelf();
        }

        Rule Bookkeeping(std::string name, int priority, Trigger trigger, std::function<void(BotContext const&)> run)
        {
            Rule rule;
            rule.name = std::move(name);
            rule.priority = priority;
            rule.trigger = std::move(trigger);
            rule.action.canDo = [](BotContext const&) { return true; };
            rule.action.run = std::move(run);
            return rule;
        }

        // Interrupts

        // A cast that the bot can interrupt now, with its interrupt spell.
        struct Interrupt
        {
            Guid caster;
            SpellId cast;
            SpellId spell; // The known rank of the interrupt spell.
        };

        // The interrupt spell (a known rank) that the bot can cast on unit
        // `caster` now, or NoSpell.
        SpellId ReadyInterrupt(BotContext const& context, Guid caster)
        {
            for (SpellId firstRank : InterruptSpells)
            {
                SpellId const rank = HighestKnownRank(context.view, firstRank);
                if (rank != NoSpell && context.view.CanCast(rank, caster))
                    return rank;
            }
            return NoSpell;
        }

        // A cast of kind `kind` by an enemy in the fight that the bot can
        // interrupt now, and that no other bot claimed.
        Interrupt FindInterrupt(BotContext const& context, CastKind kind)
        {
            for (Guid enemy : EnemiesInFight(context))
            {
                CastInfo const cast = context.view.GetCast(enemy);
                if (cast.kind != kind || !cast.interruptible)
                    continue;
                Guid const claim = context.group.GetInterruptClaim(enemy, cast.spell);
                if (claim != NoGuid && claim != Self(context))
                    continue;
                SpellId const spell = ReadyInterrupt(context, enemy);
                if (spell != NoSpell)
                    return {enemy, cast.spell, spell};
            }
            return {NoGuid, NoSpell, NoSpell};
        }

        Rule InterruptRule(std::string name, int priority, CastKind kind)
        {
            Rule rule;
            rule.name = std::move(name);
            rule.priority = priority;
            rule.trigger = [kind](BotContext const& context) { return FindInterrupt(context, kind).caster != NoGuid; };
            rule.action.canDo = [](BotContext const&) { return true; };
            rule.action.run = [kind](BotContext const& context) {
                Interrupt const interrupt = FindInterrupt(context, kind);
                // Another bot on another map thread can be first.
                if (context.group.ClaimInterrupt(interrupt.caster, interrupt.cast, Self(context)))
                    context.actions.CastSpell(interrupt.spell, interrupt.caster);
            };
            return rule;
        }

        // A claim to drop: its cast ended (the caster casts another spell,
        // nothing, or died), or it is the claim of this bot, and the bot
        // cannot interrupt the cast any more (for example the Kick missed,
        // and Kick is on cooldown now).
        bool IsClaimToDrop(BotContext const& context, InterruptClaim const& claim)
        {
            if (!IsAlive(context.view, claim.caster) || context.view.GetCast(claim.caster).spell != claim.spell)
                return true;
            return claim.bot == Self(context) && ReadyInterrupt(context, claim.caster) == NoSpell;
        }

        Guid FindClaimToDrop(BotContext const& context)
        {
            for (InterruptClaim const& claim : context.group.GetInterruptClaims())
                if (IsClaimToDrop(context, claim))
                    return claim.caster;
            return NoGuid;
        }
    }

    std::vector<Guid> EnemiesInFight(BotContext const& context)
    {
        std::vector<Guid> enemies;
        auto add = [&](Guid unit) {
            if (IsAlive(context.view, unit) && std::find(enemies.begin(), enemies.end(), unit) == enemies.end())
                enemies.push_back(unit);
        };
        add(context.group.GetFocusTarget());
        for (Guid member : context.view.GetGroupMembers())
            for (Guid attacker : context.view.GetAttackers(member))
                add(attacker);
        return enemies;
    }

    bool IsInChaseLimit(BotContext const&, Guid)
    {
        return true;
    }

    Guid PickEnemyHealer(BotContext const&)
    {
        return NoGuid;
    }

    std::shared_ptr<Strategy const> MakeGroupStrategy(BotRole)
    {
        int const P = Priority::Group;
        std::vector<Rule> rules = {
            Bookkeeping("drop an interrupt claim", P + 94,
                [](BotContext const& context) { return FindClaimToDrop(context) != NoGuid; },
                [](BotContext const& context) { context.group.ReleaseInterrupt(FindClaimToDrop(context)); }),
            InterruptRule("interrupt a heal", P + 90, CastKind::Heal),
            InterruptRule("interrupt a harmful spell", P + 85, CastKind::Harm),
        };
        return std::make_shared<Strategy const>(Strategy{"group rules", std::move(rules)});
    }
}
