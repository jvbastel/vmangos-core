#ifndef WOWAI_BOTS_THREAT_WATCH_H
#define WOWAI_BOTS_THREAT_WATCH_H

#include "BotTypes.h"

#include <cstdint>
#include <functional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// The threat losses of a scripted run. A threat loss is an enemy that hits a
// group member who is not the tank for more than 4 seconds without a break
// (spec section 10, pass thresholds). The game layer tells the watch, at
// each update of the run, which enemy hits which member:
//
//   watch.Update(hitsOnNonTanks, nowMs);
namespace Bots
{
    std::int64_t const ThreatLossMs = 4000;

    struct EnemyOnMember
    {
        Guid enemy;
        Guid member; // For the threat watch, not the tank.
    };

    // The name of a hit for the run report ("<enemy> on <member>"). The
    // watches ask for it once, when a threat loss or a lost mob starts.
    using HitNamer = std::function<std::string(EnemyOnMember const&)>;

    struct ThreatLoss
    {
        Guid enemy;
        Guid member;
        std::int64_t startMs; // The first update with the hit.
        std::int64_t endMs;   // The last update with the hit so far.
        std::string name;     // From the HitNamer, or empty.
    };

    class ThreatWatch
    {
    public:
        // A pair missing from `hits` ends its hit.
        void Update(std::vector<EnemyOnMember> const& hits, std::int64_t nowMs, HitNamer const& name = nullptr);

        // In the order they passed 4 seconds.
        std::vector<ThreatLoss> const& Losses() const { return m_losses; }

    private:
        struct Hit
        {
            EnemyOnMember pair;
            std::int64_t startMs;
            int loss; // The index in m_losses, or -1.
        };

        std::vector<Hit> m_hits;
        std::vector<ThreatLoss> m_losses;
    };

    // The lost mobs of a scripted run (WOWAI#73 review). A lost mob is an
    // enemy that hits a group member while no bot attacks it, for more than
    // the limit without a break. The tank holds an enemy that hits it while
    // the tank fights: an add that waits for its turn is in the fight. A threat loss is part of a
    // fight; a lost mob is a fight that nobody fights, so it fails the run.
    // The game layer tells the watch, at each update of the run, which
    // enemy hits which member and which enemies a bot or a pet attacks:
    //
    //   watch.Update(hitsOnMembers, attackedEnemies, nowMs, fightingTank);
    std::int64_t const LostMobMs = 10000;

    // What one group member does to enemies at an update of the run
    // (WOWAI#83). A member attacks an enemy only when it hits it or casts at
    // it: its victim (a melee swing, or the ranged attack of a caster that
    // chases to range), the unit target of the spell it casts or channels,
    // or its pet's victim. Its selection alone is no attack. The test driver
    // is no bot (`bot` false): it is a member that enemies can hit, but its
    // attacks (and its pet's) do not hold an enemy, so a lost mob shows the
    // bots' fight. A member can cast and auto-repeat at once (a hunter's
    // Aimed Shot and Auto Shot), so it keeps each spell's unit target.
    struct MemberAttacks
    {
        bool bot = true;
        Guid victim = NoGuid;
        std::vector<Guid> spellTargets;
        Guid petVictim = NoGuid;
        Guid selection = NoGuid; // Not an attack.
    };

    // The enemies that the bots attack, for LostMobWatch::Update.
    std::vector<Guid> AttackedEnemies(std::vector<MemberAttacks> const& members);

    struct LostMob
    {
        Guid enemy;
        Guid member;          // The member it hit at the last update.
        std::int64_t startMs; // The first update of the unattacked hits.
        std::int64_t endMs;   // The last update of them so far.
        std::string name;     // Of the hit when it was lost, from the HitNamer, or empty.
    };

    class LostMobWatch
    {
    public:
        explicit LostMobWatch(std::int64_t limitMs = LostMobMs) : m_limitMs(limitMs) {}

        // An enemy missing from `hits`, in `attacked`, or that hits
        // `holder` (the tank while it fights, else NoGuid) starts its time
        // again.
        void Update(std::vector<EnemyOnMember> const& hits, std::vector<Guid> const& attacked, std::int64_t nowMs,
            Guid holder = NoGuid, HitNamer const& name = nullptr);

        // In the order they passed the limit. An enemy is lost once.
        std::vector<LostMob> const& LostMobs() const { return m_lost; }
        std::int64_t LimitMs() const { return m_limitMs; }

    private:
        struct Unattacked
        {
            Guid enemy;
            std::int64_t startMs;
        };

        std::int64_t m_limitMs;
        std::vector<Unattacked> m_unattacked;
        std::vector<LostMob> m_lost;
    };

    // The whole seconds of a threat loss or a lost mob. Hit is ThreatLoss or
    // LostMob (fields name, startMs, endMs).
    template <typename Hit>
    std::int64_t HitSeconds(Hit const& hit)
    {
        return (hit.endMs - hit.startMs) / 1000;
    }

    // "<name> <s> s": a threat loss or a lost mob with its whole seconds.
    template <typename Hit>
    std::string HitText(Hit const& hit)
    {
        return hit.name + " " + std::to_string(HitSeconds(hit)) + " s";
    }

    // ", <label> <n> (<hit>; <hit>)" for the result line, or ", <label> 0".
    template <typename Hit>
    std::string HitListText(std::string const& label, std::vector<Hit> const& hits)
    {
        std::ostringstream text;
        text << ", " << label << " " << hits.size();
        for (std::size_t i = 0; i < hits.size(); ++i)
            text << (i ? "; " : " (") << HitText(hits[i]) << (i + 1 == hits.size() ? ")" : "");
        return text.str();
    }
}

// jvbastel/WOWAI#174 (the player's decision): the threat-loss clock of an
// enemy on the healer starts at its first real hit on the healer (a melee
// swing, hit or not, or a spell's damage or miss log at the healer; a DoT
// tick or a spell with no damage is not seen), not when it takes the healer
// as its victim from afar. The game layer notes each real hit; the gate lets
// a hit on a healer through to ThreatWatch only after that.
namespace Bots
{
    inline bool operator==(EnemyOnMember const& a, EnemyOnMember const& b)
    {
        return a.enemy == b.enemy && a.member == b.member;
    }

    class RealHitGate
    {
    public:
        // Enemy `enemy` swung at or cast at member `member`.
        void NoteRealHit(Guid enemy, Guid member)
        {
            m_hit.insert({enemy, member});
            m_hitters.insert(enemy);
        }
        bool HasHit(Guid enemy, Guid member) const { return m_hit.count({enemy, member}) != 0; }
        // Enemy `enemy` swung at or cast at any member.
        bool HasHitAnyone(Guid enemy) const { return m_hitters.count(enemy) != 0; }
        // The game layer notes no real hit on member `member` (no bot AI:
        // the test driver), so its hits are never held back.
        void NoteNoHitNotes(Guid member) { m_noHitNotes.insert(member); }
        bool HasNoHitNotes(Guid member) const { return m_noHitNotes.count(member) != 0; }
        // `hits` with each hit on one of `healers` left out until its first
        // real hit.
        std::vector<EnemyOnMember> Gate(std::vector<EnemyOnMember> const& hits, std::set<Guid> const& healers) const;

    private:
        std::set<std::pair<Guid, Guid>> m_hit;
        std::set<Guid> m_hitters;
        std::set<Guid> m_noHitNotes;
    };
}

// jvbastel/WOWAI#174 (the player's second decision): the lost-mob clock
// starts at the enemy's first real hit (RealHitGate, on any member: the
// clock is the enemy's, as in LostMobWatch), in scenarios and full runs. A
// hit on a member with no hit notes (the test driver) counts at once. Backstop: an enemy that targets a member and never
// hits it (it cannot reach it, or stands and waits), with no bot on it, is
// lost after LostMobBackstopMs all the same. An enemy is lost once.
namespace Bots
{
    std::int64_t const LostMobBackstopMs = 30000;

    class RealHitLostMobWatch
    {
    public:
        explicit RealHitLostMobWatch(std::int64_t limitMs = LostMobMs)
            : m_hitWatch(limitMs), m_backstop(LostMobBackstopMs) {}
        // As LostMobWatch::Update, with `hits` gated by the real hits of
        // `gate`, and ungated for the backstop.
        void Update(std::vector<EnemyOnMember> const& hits, RealHitGate const& gate, std::vector<Guid> const& attacked,
            std::int64_t nowMs, Guid holder = NoGuid, HitNamer const& name = nullptr);
        // In the order they were lost.
        std::vector<LostMob> const& LostMobs() const { return m_lost; }
        std::int64_t LimitMs() const { return m_hitWatch.LimitMs(); }

    private:
        LostMobWatch m_hitWatch;
        LostMobWatch m_backstop;
        std::vector<LostMob> m_lost;
        // For each of m_lost: from m_backstop (else m_hitWatch), and its
        // index there, so that its end follows the watch that lost it.
        std::vector<std::pair<bool, std::size_t>> m_from;
    };
}

#endif
