#ifndef WOWAI_BOTS_FIGHT_WATCH_H
#define WOWAI_BOTS_FIGHT_WATCH_H

#include "BotTypes.h"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

// Who took part in a fight of a scripted run (jvbastel/WOWAI#98). A fight
// starts at a pull and ends when the group leaves combat. A member took part
// when it attacked or cast at a mob of the fight, or cast a heal at a
// member. A target alone is no part: a member that only follows the tank
// targets the mob too. A member with no part "did not join".
//
// The watch takes the same observations as the trace (RunTrace::Observe):
// the victim and the current cast of each member. A cast counts once, at the
// update that first sees it; an instant cast is never a current cast, so the
// count holds only the casts with a cast time or a channel.
namespace Bots
{
    // What the watch sees of one member at one update.
    struct FightMemberSeen
    {
        std::string name;
        Guid victim;       // The unit that it attacks, or NoGuid.
        SpellId spell;     // The spell that it casts now, or NoSpell.
        Guid spellTarget;  // The unit of that cast, or NoGuid.
        CastKind kind;     // The kind of that cast.
    };

    struct FightMember
    {
        std::string name;
        bool joined = false;
        std::int64_t firstActionMs = 0; // From the pull to its first part.
        std::uint32_t casts = 0;        // At a mob of the fight, and heals at a member.
    };

    struct FightSummary
    {
        std::uint32_t number = 0; // The pull of the run that started it: 1 for the first.
        std::string pack;         // Empty for a `command pull`.
        bool again = false;       // A fight again of a pack that lived after its fight.
        std::int64_t startMs = 0;
        std::int64_t endMs = 0;
        std::vector<FightMember> members; // In the order of Start.
    };

    class FightWatch
    {
    public:
        // A pull starts fight `number`. `members` are the names and guids of
        // the group, the test driver too. A fight that still runs is dropped:
        // End it first.
        void Start(std::uint32_t number, std::string const& pack, bool again, std::int64_t ms,
            std::vector<std::pair<std::string, Guid>> const& members);
        bool Running() const { return m_running; }

        // One update: whether the group is in combat, the enemies that the
        // group fights now, and what each member does. True when the fight
        // is over: the group was in combat and has left it. Then End it.
        bool Observe(std::int64_t ms, bool groupInCombat, std::set<Guid> const& enemies,
            std::vector<FightMemberSeen> const& members);

        // The summary of the fight, which then no longer runs.
        FightSummary End(std::int64_t ms);

    private:
        struct Action
        {
            std::int64_t ms;
            Guid target;
            bool cast;
            CastKind kind;
        };

        struct Watched
        {
            Guid guid;
            Guid victim = NoGuid;
            SpellId spell = NoSpell;
            Guid spellTarget = NoGuid;
            std::vector<Action> actions;
        };

        bool m_running = false;
        bool m_inCombat = false;
        FightSummary m_fight;
        std::vector<std::string> m_order;
        std::map<std::string, Watched> m_members;
        std::set<Guid> m_enemies; // Each enemy of the fight so far.
    };

    // "fight 3 pack12: Grukk 0.4 s 3 casts, Hela 2.1 s 5 casts, Borin did
    // not join", for the log and the trace.
    std::string FightText(FightSummary const& summary);
}

#endif
