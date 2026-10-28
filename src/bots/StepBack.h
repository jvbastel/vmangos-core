#ifndef WOWAI_BOTS_STEP_BACK_H
#define WOWAI_BOTS_STEP_BACK_H

#include "BotRecipe.h"
#include "BotView.h"
#include "PriestModule.h"
#include "Strategy.h"

#include <vector>

// The step back (jvbastel/WOWAI#144, spec #142). Frost Nova freezes the
// enemies round the Mage (the peel nova, MageModule.h) for up to 8 s. A
// ranged member or the healer within StepBackYards of a frozen enemy walks
// back along the cleared way of the fight (GroupCoordinator::GetFightWay)
// until it stands StepBackYards from every frozen enemy, so that none can
// reach it until the tank picks it up. It walks at most about
// StepBackMostYards. When the way back gives no such spot (it is too long,
// a frozen enemy stands on it, or there is no way), the bot goes to the
// nearest safe place round it instead (SafePlace.h). Each spot, and the
// path to it, keeps out of the packs outside the fight (KeepOut.h), and the
// path goes no nearer to a frozen enemy within AggroMarginYards. The
// healer's spot keeps the tank in StepBackHealYards and in sight. After the
// step back the bot does its job from where it stands: the rule fires only
// while a frozen enemy is in reach.
namespace Bots
{
    // A frozen enemy reaches a member this near (between the centers).
    float const StepBackYards = 8.0f;
    // The longest walk of a step back along the cleared way.
    float const StepBackMostYards = 15.0f;
    // The healer's spot keeps the tank this near: the heal reach of the
    // Priest (PriestModule.h).
    float const StepBackHealYards = Priest::HealReachYards;

    // The enemies near the bot that Frost Nova holds frozen now (IsFrozen,
    // DamageRules.h), within `yards`.
    std::vector<NearEnemy> FrozenEnemiesNear(BotView const& view, float yards);

    // The spot of the bot of role `role` for its step back. False when no
    // frozen enemy is within StepBackYards of it, and when no spot is safe.
    bool FindStepBackSpot(BotContext const& context, BotRole role, Position& spot);

    // The rule "step back from a frozen enemy" of a ranged member or the
    // healer (role `role`), at `priority` in its class module.
    Rule StepBackRule(int priority, BotRole role);
    char const* const StepBackRuleName = "step back from a frozen enemy";
}

#endif
