#include "PriestModule.h"
#include "CommonRules.h"
#include "DamageRules.h"
#include "Ranged.h"
#include "SafePlace.h"
#include "SpellBook.h"

namespace Bots
{
    namespace Priest
    {
        namespace
        {
            // Health fractions: a member below one of them needs that heal.
            float const ShieldBelow = 0.3f;
            float const HealBelow = 0.5f;
            float const LesserHealBelow = 0.7f;
            float const RenewBelow = 0.9f;

            // jvbastel/WOWAI#111: in a pull, the healer waits this long
            // after the tank engages (HoldPull) before its first heal, so
            // that the tank has threat on every enemy first: heal threat
            // goes to every enemy in the fight. A member below DangerBelow
            // gets a heal at once.
            std::int64_t const HealDelayMs = 2000;
            float const DangerBelow = 0.5f;

            // The heals reach 40 yards. The bot goes nearer to a hurt member
            // beyond this distance.
            float const HealReachYards = 36.0f;
            // Resurrection reaches 30 yards. A corpse beyond this distance is
            // out of reach, and the bot casts from this near.
            float const ResurrectReachYards = 29.0f;
            float const ResurrectCastYards = 26.0f;

            using MemberFilter = bool (*)(BotView const& view, Guid member);

            bool AnyMember(BotView const&, Guid)
            {
                return true;
            }

            bool CanTakeShield(BotView const& view, Guid member)
            {
                return !view.HasAura(member, WeakenedSoul) && !view.HasAura(member, PowerWordShield);
            }

            bool HasNoRenew(BotView const& view, Guid member)
            {
                return !view.HasAura(member, Renew);
            }

            float HealthFraction(BotView const& view, Guid unit)
            {
                Health const health = view.GetHealth(unit);
                return health.max ? float(health.current) / float(health.max) : 1.0f;
            }

            // The living group member with the lowest health fraction below
            // `below` that `filter` accepts, or NoGuid. On a tie, the first
            // member.
            Guid MostHurtMember(BotContext const& context, float below, MemberFilter filter)
            {
                Guid most = NoGuid;
                float lowest = below;
                for (Guid member : context.view.GetGroupMembers())
                {
                    float const fraction = HealthFraction(context.view, member);
                    if (IsAlive(context.view, member) && fraction < lowest && filter(context.view, member))
                    {
                        most = member;
                        lowest = fraction;
                    }
                }
                return most;
            }

            // An enemy attacks the bot in a fight.
            bool IsAttacked(BotContext const& context)
            {
                Guid const self = PickSelf(context);
                return context.view.IsInCombat(self) && !context.view.GetAttackers(self).empty();
            }

            bool OutOfCombat(BotContext const& context)
            {
                return !context.view.IsInCombat(PickSelf(context));
            }

            // jvbastel/WOWAI#133: the bot shoots the focus target with its
            // wand: in a fight, once the tank holds threat on it
            // (TankHoldsThreat, DamageRules.h), with no enemy on the bot,
            // the target in shot range and sight, and no shot going yet.
            // Shoot costs no mana: the spirit regen goes on.
            bool ShootsTheFocus(BotContext const& context)
            {
                Guid const focus = context.group.GetFocusTarget();
                return context.view.IsInCombat(PickSelf(context)) && !IsAttacked(context)
                    && context.view.GetRangedWeapon().kind == RangedKind::Wand && !context.view.IsShooting()
                    && IsAlive(context.view, focus) && TankHoldsThreat(context) && InWandShot(context.view, focus);
            }

            Guid PickFocus(BotContext const& context)
            {
                return context.group.GetFocusTarget();
            }

            // The first living member without Power Word: Fortitude, or NoGuid.
            Guid MemberWithoutFortitude(BotContext const& context)
            {
                for (Guid member : context.view.GetGroupMembers())
                    if (IsAlive(context.view, member) && !context.view.HasAura(member, PowerWordFortitude))
                        return member;
                return NoGuid;
            }

            // A dead member that has not released, and whose resurrect the
            // bot has not given up.
            bool MayResurrect(BotContext const& context, Guid member)
            {
                return context.view.GetLifeState(member) == LifeState::Dead
                    && !context.group.IsResurrectGivenUp(member);
            }

            // The first dead member that the bot can resurrect now, or NoGuid.
            Guid DeadMemberInReach(BotContext const& context)
            {
                for (Guid member : context.view.GetGroupMembers())
                    if (MayResurrect(context, member) && CastableRank(context.view, Resurrection, member) != NoSpell)
                        return member;
                return NoGuid;
            }

            // The first dead member beyond the reach of the resurrect, or out
            // of the bot's sight (a wall, a step), or NoGuid. A corpse in
            // reach but out of sight counts too: else no rule would take it,
            // and the dead member would wait for a resurrect that never comes.
            Guid DeadMemberOutOfReach(BotContext const& context)
            {
                if (!context.view.KnowsSpell(Resurrection))
                    return NoGuid;
                Position const here = context.view.GetPosition(PickSelf(context));
                for (Guid member : context.view.GetGroupMembers())
                    if (MayResurrect(context, member) && (context.view.GetDistance(member) > ResurrectReachYards
                        || !context.view.CanSeeFrom(here, member)))
                        return member;
                return NoGuid;
            }

            // The bot walks to a safe place in cast range of the corpse. With
            // none, it does not go: it gives the resurrect up, and the dead
            // member runs back as a ghost. An enemy that came near the place
            // on the way: the bot stops where it stands, and gives the
            // resurrect up too. The enemies of the search block it: when one
            // of them dies, the bot tries again (WatchGroup).
            void GoToCorpse(BotContext const& context)
            {
                Guid const member = DeadMemberOutOfReach(context);
                Position place;
                std::vector<NearEnemy> enemies;
                if (FindSafeCastPlace(context.view, member, ResurrectCastYards, place, &enemies))
                    context.actions.KeepMovingTo(place);
                else
                {
                    std::vector<Guid> blockers;
                    for (NearEnemy const& enemy : enemies)
                        if (enemy.guid != NoGuid)
                            blockers.push_back(enemy.guid);
                    context.group.GiveUpResurrect(member, blockers);
                    context.actions.MoveTo(context.view.GetPosition(PickSelf(context)));
                }
            }

            // The most hurt member that needs a heal and is beyond the heal
            // reach, or NoGuid.
            Guid HurtMemberOutOfReach(BotContext const& context)
            {
                Guid const member = MostHurtMember(context, RenewBelow, AnyMember);
                return member != NoGuid && context.view.GetDistance(member) > HealReachYards ? member : NoGuid;
            }

            // In a pull, before the tank engaged or within HealDelayMs after
            // it, while no member is in danger: the healer waits. When the
            // pull target attacks someone other than the tank, the tank
            // never engages it, and the healer does not wait for that.
            bool WaitsForTheTank(BotContext const& context)
            {
                PullState const pull = context.group.GetPull();
                Guid const victim = pull.phase == PullPhase::Pulling ? context.view.GetVictim(pull.target) : NoGuid;
                bool const early = (pull.phase == PullPhase::Pulling && (victim == NoGuid || victim == pull.puller))
                    || (pull.phase == PullPhase::Holding && context.nowMs - pull.engagedMs < HealDelayMs);
                return early && MostHurtMember(context, DangerBelow, AnyMember) == NoGuid;
            }

            TargetPicker HurtMember(float below, MemberFilter filter = AnyMember)
            {
                return [below, filter](BotContext const& context) { return MostHurtMember(context, below, filter); };
            }

            Trigger SomeoneBelow(float below, MemberFilter filter = AnyMember)
            {
                return [below, filter](BotContext const& context) {
                    return !WaitsForTheTank(context) && MostHurtMember(context, below, filter) != NoGuid;
                };
            }
        }

        std::shared_ptr<Strategy const> MakeHealerStrategy()
        {
            int const P = Priority::Class;
            std::vector<Rule> rules = {
                // An enemy attacks the Priest: it never fights back. It drops
                // its threat with Fade, so that the enemy goes back to the
                // tank, and shields itself.
                CastRule("fade", P + 90, IsAttacked, Fade, PickSelf),
                CastRule("shield itself", P + 86,
                    [](BotContext const& context) {
                        return IsAttacked(context) && CanTakeShield(context.view, PickSelf(context));
                    },
                    PowerWordShield, PickSelf),

                // The heals, the most urgent first. Each picks the most
                // hurt member that needs it. At level 16 the Priest knows no
                // dispel: it heals through every debuff.
                CastRule("shield a member near death", P + 88, SomeoneBelow(ShieldBelow, CanTakeShield),
                    PowerWordShield, HurtMember(ShieldBelow, CanTakeShield)),
                CastRule("heal", P + 80, SomeoneBelow(HealBelow), Heal, HurtMember(HealBelow)),
                CastRule("lesser heal a badly hurt member", P + 75, SomeoneBelow(HealBelow), LesserHeal, HurtMember(HealBelow)),
                CastRule("renew", P + 70, SomeoneBelow(RenewBelow, HasNoRenew), Renew, HurtMember(RenewBelow, HasNoRenew)),
                CastRule("lesser heal", P + 65, SomeoneBelow(LesserHealBelow), LesserHeal, HurtMember(LesserHealBelow)),

                // A hurt member out of reach: the bot follows it. It stays
                // near it until the fight ends and the command takes over.
                Rule{"go to a hurt member", P + 50,
                    [](BotContext const& context) {
                        Guid const member = HurtMemberOutOfReach(context);
                        return member != NoGuid && context.view.GetFollowTarget() != member;
                    },
                    BotAction{[](BotContext const&) { return true; },
                        [](BotContext const& context) { context.actions.Follow(HurtMemberOutOfReach(context)); }}},

                // After the fight, under the heals: the living first. Any
                // dead member in reach, then a corpse out of reach.
                CastRule("resurrect a member", P + 45,
                    [](BotContext const& context) {
                        return !IsGroupInCombat(context.view) && DeadMemberInReach(context) != NoGuid;
                    },
                    Resurrection, DeadMemberInReach),
                Rule{"go to a corpse out of reach", P + 44,
                    [](BotContext const& context) {
                        return !IsGroupInCombat(context.view) && DeadMemberOutOfReach(context) != NoGuid;
                    },
                    BotAction{[](BotContext const&) { return true; }, GoToCorpse}},

                // jvbastel/WOWAI#133: between heals, the wand on the focus
                // target. Each heal comes first, and its cast stops the shot.
                CastRule("shoot the focus target", P + 30, ShootsTheFocus, Shoot, PickFocus),

                // The buffs, out of combat only: in a fight the mana is for
                // the heals.
                CastRule("fortitude", P + 20,
                    [](BotContext const& context) {
                        return OutOfCombat(context) && MemberWithoutFortitude(context) != NoGuid;
                    },
                    PowerWordFortitude, MemberWithoutFortitude),
                CastRule("inner fire", P + 15,
                    [](BotContext const& context) {
                        return OutOfCombat(context) && !context.view.HasAura(PickSelf(context), InnerFire);
                    },
                    InnerFire, PickSelf),
            };
            return std::make_shared<Strategy const>(Strategy{"priest healer", std::move(rules)});
        }
    }
}
