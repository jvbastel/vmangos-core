#include "BossReactions.h"
#include "BossFights.h"
#include "CommonRules.h"
#include "TaragamanReaction.h"

namespace Bots
{
    namespace
    {
        struct BossReaction
        {
            std::uint32_t entry;
            std::shared_ptr<Strategy const> (*make)(BotRole role);
        };

        BossReaction const Reactions[] = {
            {Taragaman::Entry, &Taragaman::MakeReaction},
        };

        BossReaction const* Find(std::uint32_t entry)
        {
            for (BossReaction const& reaction : Reactions)
                if (reaction.entry == entry)
                    return &reaction;
            return nullptr;
        }

        // The bot sees boss `boss` dead, or out of combat.
        bool SeesFightEnd(BotView const& view, Guid boss)
        {
            Health const health = view.GetHealth(boss);
            return health.max != 0 && (health.current == 0 || !view.IsInCombat(boss));
        }
    }

    bool HasBossReaction(std::uint32_t entry)
    {
        return Find(entry) != nullptr;
    }

    std::shared_ptr<Strategy const> MakeBossReaction(std::uint32_t entry, BotRole role)
    {
        BossReaction const* reaction = Find(entry);
        return reaction ? reaction->make(role) : nullptr;
    }

    BossReactionSlot::Change BossReactionSlot::Update(BotView const& view, GroupCoordinator& group,
        StrategyStack& stack, BotRole role)
    {
        BossFight fight = group.GetBossFight();
        if (fight.boss != NoGuid && SeesFightEnd(view, fight.boss))
        {
            group.EndBossFight(fight.boss);
            // After a wipe he lives: the focus on him must end too, else the
            // damage bots attack him again as soon as they revive.
            if (group.GetFocusTarget() == fight.boss)
                group.EndPull();
            fight = group.GetBossFight();
        }
        if (fight.boss == NoGuid)
        {
            Guid const boss = FindBossInFight(view, [&view](Guid unit) { return HasBossReaction(view.GetEntry(unit)); });
            if (boss != NoGuid)
            {
                group.StartBossFight(boss, view.GetEntry(boss));
                fight = group.GetBossFight();
            }
        }

        Change change = Change::None;
        if (!m_active.empty() && fight.boss != m_boss)
        {
            stack.Remove(m_active);
            m_active.clear();
            m_boss = NoGuid;
            change = Change::Popped;
        }
        if (m_active.empty() && fight.boss != NoGuid)
        {
            if (std::shared_ptr<Strategy const> reaction = MakeBossReaction(fight.entry, role))
            {
                m_active = reaction->name;
                m_boss = fight.boss;
                stack.Push(std::move(reaction));
                change = Change::Pushed;
            }
        }
        return change;
    }
}
