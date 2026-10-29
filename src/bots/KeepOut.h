#ifndef WOWAI_BOTS_KEEP_OUT_H
#define WOWAI_BOTS_KEEP_OUT_H

#include "BotView.h"
#include "Strategy.h"

#include <cstdint>
#include <memory>
#include <vector>

// The packs outside the fight (jvbastel/WOWAI#121): the living enemies that
// are in no combat, but the pack of the enemy that the group fights. A
// member that stands within the aggro distance and AggroMarginYards of one
// of them (PullsAnEnemy, SafePlace.h) may pull its pack into the fight.
namespace Bots
{
    // The enemies round a place that a member there could pull: more than
    // the aggro distance of any creature and the margin.
    float const KeepOutSearchYards = 50.0f;

    // The enemies outside the fight round `at`, that is: in no combat, and
    // not in the pack of `fightTarget` (SplitTargetPack, FightSpot.h).
    std::vector<NearEnemy> EnemiesOutsideTheFight(BotView const& view, Position const& at, Guid fightTarget);

    // The packs outside the fight that a member at `at` pulls. 0: a member
    // may stand at `at`.
    std::uint32_t PacksPulledAt(BotView const& view, Position const& at, Guid fightTarget);

    // The enemy that the group fights: the focus, else the pull target.
    // The packs outside the fight are those of the others.
    Guid FightTarget(BotContext const& context);

    // A member at `at` pulls a pack outside the fight of FightTarget: the
    // one place check of the keep-out rule, and of the moves that must not
    // go there (the tank's go-out to a member, WarriorModule.cpp).
    bool InOtherPacksReach(BotContext const& context, Position const& at);

    // Member `member` stands in the reach of a pack outside the fight, and
    // finds no safe place to step out to: the keep-out rule cannot move it.
    bool StuckInOtherPacksReach(BotContext const& context, Guid member);

    // Review of jvbastel/WOWAI#126 and #121: enemy `enemy` hits the healer
    // and stands in the reach of a pack outside the fight, and the healer is
    // stuck there (StuckInOtherPacksReach). The tank picks it up all the
    // same, after a warn (WarriorModule.cpp), and the keep-out rule lets the
    // tank that hits it stand there.
    bool TankGoesInForTheHealer(BotContext const& context, Guid enemy);

    // Enemy `enemy` hits the living tank in melee contact, and the tank
    // stands in the reach of a pack outside the fight itself. A melee member
    // may hit the enemy there (PickMeleeDamageTarget, DamageRules.h): it
    // stands where the tank stands already, and the tank, not the member,
    // decides to fight in that place. The keep-out rule lets a member in
    // melee contact with such an enemy stay.
    bool TankHoldsItInOtherPacksReach(BotContext const& context, Guid enemy);

    // The strategy "keep out": its rule KeepOutRule, for each bot, the tank
    // too, at Priority::Group + 82. While a pull holds, or the bot is in combat, a bot
    // that stands within the aggro distance and AggroMarginYards of an
    // enemy outside the fight of the group's focus steps out to a safe place
    // (FirstSafePlace, SafePlace.h). With no safe place the rule does not
    // fire: the bot keeps its fight rather than freeze. The exceptions: the
    // puller that goes in to a caster (PullState::goesIn, WarriorModule.h),
    // the tank that hits an add of TankGoesInForTheHealer, a member in
    // melee contact with an enemy of TankHoldsItInOtherPacksReach, and the puller
    // while it has its fight spot (it pulls, or walks back there): the
    // fight spot search chose that way.
    std::shared_ptr<Strategy const> MakeKeepOutStrategy();
    char const* const KeepOutRule = "keep out of other packs";
}

#endif
