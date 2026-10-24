#include "DamageRules.h"
#include "CommonRules.h"

namespace Bots
{
    bool MayAttackFocus(BotContext const& context)
    {
        return IsAlive(context.view, context.group.GetFocusTarget())
            && context.group.GetPull().phase != PullPhase::Pulling;
    }
}
