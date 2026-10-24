#include "SpellFile.h"
#include "DataTokenizer.h"

#include <algorithm>
#include <limits>

namespace Bots
{
    namespace
    {
        bool ParsePositive(std::string const& word, std::uint32_t& value)
        {
            if (word.empty() || word.size() > 10 || word.find_first_not_of("0123456789") != std::string::npos)
                return false;
            unsigned long long const number = std::stoull(word);
            if (number == 0 || number > std::numeric_limits<std::uint32_t>::max())
                return false;
            value = static_cast<std::uint32_t>(number);
            return true;
        }
    }

    SpellFileResult ParseSpellFile(std::string const& text)
    {
        SpellFileResult result{false, {}, ""};
        for (DataLine const& line : TokenizeLines(text))
        {
            std::string const where = "Line " + std::to_string(line.number) + ": ";
            std::string const& kindWord = line.words[0];
            ClassSpell spell{SpellKind::Quest, 0, 0};
            if (kindWord == "quest-spell")
                spell.kind = SpellKind::Quest;
            else if (kindWord == "pet-spell")
                spell.kind = SpellKind::Pet;
            else
            {
                result.error = where + "unknown line kind '" + kindWord + "'.";
                return result;
            }
            if (line.words.size() != 3)
            {
                result.error = where + "write '" + kindWord + " <level> <spellId>'.";
                return result;
            }
            for (std::size_t i = 1; i < 3; ++i)
            {
                if (!ParsePositive(line.words[i], i == 1 ? spell.level : spell.spellId))
                {
                    result.error = where + "'" + line.words[i] + "' is not a positive number.";
                    return result;
                }
            }
            result.spells.push_back(spell);
        }
        result.ok = true;
        return result;
    }

    std::vector<std::uint32_t> SpellsUpToLevel(std::vector<ClassSpell> const& spells, SpellKind kind, std::uint32_t level)
    {
        std::vector<ClassSpell> chosen;
        for (ClassSpell const& spell : spells)
            if (spell.kind == kind && spell.level <= level)
                chosen.push_back(spell);
        std::stable_sort(chosen.begin(), chosen.end(),
            [](ClassSpell const& a, ClassSpell const& b) { return a.level < b.level; });
        std::vector<std::uint32_t> ids;
        for (ClassSpell const& spell : chosen)
            ids.push_back(spell.spellId);
        return ids;
    }
}
