#ifndef WOWAI_BOTS_EXTRA_PACK_WATCH_H
#define WOWAI_BOTS_EXTRA_PACK_WATCH_H

#include "RunResult.h"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

// The extra packs of a pull in a scripted run (jvbastel/WOWAI#103): the
// packs of the route that joined the fight of the pull, other than the pack
// of the pull itself, and whether the tank warned about them in party chat
// (BotActions::WarnExtraPacks). A mob of no pack of the route (a roaming mob)
// counts for no pack.
namespace Bots
{
    class ExtraPackWatch
    {
    public:
        ExtraPackWatch() = default;
        // The pack of each spawn guid of the route.
        explicit ExtraPackWatch(std::map<std::uint32_t, std::string> packOfSpawn);

        // A new pull of `pack` (empty when the pull has no pack of the
        // route: then each pack that joins is extra). It drops the extra
        // packs and the warn of the last pull.
        void Start(std::string const& pack);
        // The spawn guids of the enemies that the group fights now.
        void Observe(std::int64_t ms, std::set<std::uint32_t> const& enemySpawns);
        // The tank warned about extra packs in party chat: each extra pack of
        // the pull counts as warned, also one that joins later.
        void NoteWarn();

        // The extra packs of the pull, in the order they joined.
        std::vector<RunExtraPack> const& ExtraPacks() const { return m_extra; }
        // The pack of the spawn, or "".
        std::string PackOf(std::uint32_t spawnGuid) const;

    private:
        std::map<std::uint32_t, std::string> m_packOf;
        std::string m_pack;
        bool m_warned = false;
        std::vector<RunExtraPack> m_extra;
    };
}

#endif
