#ifndef WOWAI_BOTS_BOT_ACTIONS_H
#define WOWAI_BOTS_BOT_ACTIONS_H

#include "BotTypes.h"
#include "Ranged.h"

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
        // Move to `destination`, on a new path.
        virtual void MoveTo(Position const& destination) = 0;
        // Move to `destination`, but a bot that already walks to about that
        // spot keeps its path: for a long walk that a rule repeats each
        // think, such as the walk to a corpse. A new path each think would
        // start the walk again.
        virtual void KeepMovingTo(Position const& destination) = 0;
        // Start the melee attack of unit `target`.
        virtual void Attack(Guid target) = 0;
        // Run to unit `target` and stay in melee range of it.
        virtual void Chase(Guid target) = 0;
        // Run behind unit `target` and stay in melee range there.
        virtual void ChaseBehind(Guid target) = 0;
        // Run to unit `target` until it is `yards` away, and stay about that
        // far from it: the chase of a ranged bot.
        virtual void ChaseToRange(Guid target, float yards) = 0;
        // Stop where the bot stands: no walk and no chase.
        virtual void Stand() = 0;
        // The pet of the bot attacks unit `target`.
        virtual void PetAttack(Guid target) = 0;
        // Follow unit `unit` at a short distance.
        virtual void Follow(Guid unit) = 0;
        // Do the next step of the corpse loot: walk to the corpse, open it,
        // take the loot, or close it. `mayMove`: as in BotView::HasLootWork.
        virtual void Loot(bool mayMove) = 0;

        // Release the spirit of the dead bot, like the release button of a
        // player: the bot becomes a ghost at the graveyard.
        virtual void Release() = 0;
        // The next step of the ghost run of a released bot, as a player
        // runs it: from the graveyard along the ghost route of the dungeon
        // of its corpse to the portal, and in. The bot is alive again at
        // the dungeon entrance. In a scripted run the bot waits instead:
        // the run revives it (spec section 10).
        virtual void WalkAsGhost() = 0;
        // Sit and eat the food of the bot's level. The bot's food refills
        // silently, so the bot never runs out.
        virtual void Eat() = 0;
        // Sit and drink the water of the bot's level. It refills silently.
        virtual void Drink() = 0;
        // Bring the ammo up to `topUp` silently, from nothing: no vendor
        // and no money (Ranged.h). The arrows or bullets go in the bags and
        // become the ammo of the ammo slot; a thrown stack grows in place.
        virtual void TopUpAmmo(AmmoTopUp const& topUp) = 0;
    };
}

#endif
