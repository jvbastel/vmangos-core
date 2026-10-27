#include "ExtraPackWatch.h"

#include <algorithm>
#include <utility>

namespace Bots
{
    ExtraPackWatch::ExtraPackWatch(std::map<std::uint32_t, std::string> packOfSpawn) : m_packOf(std::move(packOfSpawn))
    {
    }

    void ExtraPackWatch::Start(std::string const& pack)
    {
        m_pack = pack;
        m_warned = false;
        m_extra.clear();
    }

    void ExtraPackWatch::Observe(std::int64_t ms, std::set<std::uint32_t> const& enemySpawns)
    {
        for (std::uint32_t spawn : enemySpawns)
        {
            std::string const pack = PackOf(spawn);
            if (pack.empty() || pack == m_pack)
                continue;
            bool const known = std::any_of(m_extra.begin(), m_extra.end(),
                [&pack](RunExtraPack const& extra) { return extra.pack == pack; });
            if (!known)
                m_extra.push_back({pack, ms, m_warned});
        }
    }

    void ExtraPackWatch::NoteWarn()
    {
        m_warned = true;
        for (RunExtraPack& extra : m_extra)
            extra.warned = true;
    }

    std::string ExtraPackWatch::PackOf(std::uint32_t spawnGuid) const
    {
        auto const it = m_packOf.find(spawnGuid);
        return it == m_packOf.end() ? std::string() : it->second;
    }
}
