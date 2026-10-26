#ifndef WOWAI_BOTS_SPELL_FILE_H
#define WOWAI_BOTS_SPELL_FILE_H

#include <cstdint>
#include <string>
#include <vector>

// The spell file of a class, `<Bots.DataDir>/spells/<class>.txt`. It lists the
// spells that the level routine teaches and that no trainer teaches:
//
//   quest-spell <level> <spellId>   # a class quest spell, e.g. Summon Voidwalker
//   pet-spell <level> <spellId>     # a spell of the class pet, e.g. Firebolt (Rank 2)
//
// The spell name goes in the comment. The trainer spells are not in the file:
// the game layer reads them from the trainer tables.
namespace Bots
{
    enum class SpellKind
    {
        Quest, // The character learns it.
        Pet,   // The pet of the character learns it.
    };

    struct ClassSpell
    {
        SpellKind kind;
        std::uint32_t level; // The character level from which on the spell is known.
        std::uint32_t spellId;
    };

    struct SpellFileResult
    {
        bool ok;
        std::vector<ClassSpell> spells; // Only when ok, in file order.
        std::string error;              // When not ok: the first bad line.
    };

    SpellFileResult ParseSpellFile(std::string const& text);

    // The spells of one kind up to a level, lower levels first, so that the
    // lower rank of a spell comes before the higher rank.
    std::vector<std::uint32_t> SpellsUpToLevel(std::vector<ClassSpell> const& spells, SpellKind kind, std::uint32_t level);
}

#endif
