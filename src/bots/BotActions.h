#ifndef WOWAI_BOTS_BOT_ACTIONS_H
#define WOWAI_BOTS_BOT_ACTIONS_H

#include "BotTypes.h"

namespace Bots
{
    // The deeds of one bot in the game. With BotView, the only way from the
    // decision library to the game (ADR 0002 of WOWAI).
    class BotActions
    {
    public:
        virtual ~BotActions() = default;

        // Cast spell `spell` on unit `target`.
        virtual void CastSpell(SpellId spell, Guid target) = 0;
        // Move to `destination`.
        virtual void MoveTo(Position const& destination) = 0;
        // Start the melee attack of unit `target`.
        virtual void Attack(Guid target) = 0;
        // Run to unit `target` and stay in melee range of it.
        virtual void Chase(Guid target) = 0;
        // Follow unit `unit` at a short distance.
        virtual void Follow(Guid unit) = 0;
        // Do the next step of the corpse loot: walk to the corpse, open it,
        // take the loot, or close it. `mayMove`: as in BotView::HasLootWork.
        virtual void Loot(bool mayMove) = 0;

        // Release the spirit of the dead bot, like the release button of a
        // player: the bot becomes a ghost at the graveyard.
        virtual void Release() = 0;
        // Revive the dead bot at its corpse at once, with no ghost run. A
        // bot that has not released releases first.
        virtual void ReviveAtCorpse() = 0;
        // Sit and eat the food of the bot's level. The bot's food refills
        // silently, so the bot never runs out.
        virtual void Eat() = 0;
        // Sit and drink the water of the bot's level. It refills silently.
        virtual void Drink() = 0;
    };
}

#endif
