#include "GearTemplate.h"
#include "DataTokenizer.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace Bots
{
    namespace
    {
        struct SlotInfo
        {
            GearSlot slot;
            char const* name;
        };

        SlotInfo const Slots[] = {
            {GearSlot::Head, "head"},
            {GearSlot::Neck, "neck"},
            {GearSlot::Shoulders, "shoulders"},
            {GearSlot::Back, "back"},
            {GearSlot::Chest, "chest"},
            {GearSlot::Shirt, "shirt"},
            {GearSlot::Tabard, "tabard"},
            {GearSlot::Wrists, "wrists"},
            {GearSlot::Hands, "hands"},
            {GearSlot::Waist, "waist"},
            {GearSlot::Legs, "legs"},
            {GearSlot::Feet, "feet"},
            {GearSlot::Finger, "finger"},
            {GearSlot::Trinket, "trinket"},
            {GearSlot::Ranged, "ranged"},
            {GearSlot::MainHand, "mainhand"},
            {GearSlot::OffHand, "offhand"},
            {GearSlot::TwoHand, "twohand"},
            {GearSlot::Shield, "shield"},
            {GearSlot::Holdable, "holdable"},
        };

        bool ParseSlot(std::string const& word, GearSlot& slot)
        {
            for (SlotInfo const& info : Slots)
                if (word == info.name)
                {
                    slot = info.slot;
                    return true;
                }
            return false;
        }

        // A whole number with digits only.
        bool ParseNumber(std::string const& word, std::uint32_t& value)
        {
            if (word.empty() || word.size() > 9)
                return false;
            for (char c : word)
                if (c < '0' || c > '9')
                    return false;
            value = static_cast<std::uint32_t>(std::strtoul(word.c_str(), nullptr, 10));
            return true;
        }

        bool ParseScore(std::string const& word, double& value)
        {
            if (word.empty())
                return false;
            char* end = nullptr;
            value = std::strtod(word.c_str(), &end);
            return end && *end == '\0';
        }

        bool ParseLevels(std::string const& word, std::uint32_t& minLevel, std::uint32_t& maxLevel)
        {
            std::string::size_type dash = word.find('-');
            return dash != std::string::npos &&
                ParseNumber(word.substr(0, dash), minLevel) &&
                ParseNumber(word.substr(dash + 1), maxLevel);
        }

        std::uint32_t const MaxLevel = 60;
        std::uint32_t const MaxPatch = 10; // WOW_PATCH_112 of the core.

        bool IsWeaponSlot(GearSlot slot)
        {
            return slot == GearSlot::MainHand || slot == GearSlot::OffHand || slot == GearSlot::TwoHand ||
                slot == GearSlot::Shield || slot == GearSlot::Holdable;
        }

        bool FillsMainHand(GearSlot slot)
        {
            return slot == GearSlot::MainHand || slot == GearSlot::TwoHand;
        }

        bool FillsOffHand(GearSlot slot)
        {
            return slot == GearSlot::OffHand || slot == GearSlot::TwoHand || slot == GearSlot::Shield ||
                slot == GearSlot::Holdable;
        }

        bool IsValidName(std::string const& name)
        {
            if (name.empty())
                return false;
            for (char c : name)
                if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
                    return false;
            return true;
        }

        std::string WithArticle(char const* word)
        {
            bool vowel = std::string("aeiou").find(word[0]) != std::string::npos;
            return std::string(vowel ? "An " : "A ") + word;
        }

        GearTemplateResult Fail(std::size_t line, std::string const& error)
        {
            GearTemplateResult result;
            result.ok = false;
            result.error = "line " + std::to_string(line) + ": " + error;
            return result;
        }

        // The counted positions of a slot: finger and trinket are one counted
        // position with room for two items.
        std::vector<std::string> CountedPositions(GearSlot slot)
        {
            if (IsWeaponSlot(slot))
            {
                std::vector<std::string> positions;
                if (FillsMainHand(slot))
                    positions.push_back("mainhand");
                if (FillsOffHand(slot))
                    positions.push_back("offhand");
                return positions;
            }
            return {GearSlotName(slot)};
        }

        std::size_t Capacity(std::string const& position)
        {
            return position == "finger" || position == "trinket" ? 2 : 1;
        }

        bool SharesPosition(GearItem const& item, std::string const& position)
        {
            for (std::string const& p : CountedPositions(item.slot))
                if (p == position)
                    return true;
            return false;
        }

        bool SharesFaction(GearItem const& a, GearItem const& b)
        {
            return a.factions == GearFactions::Both || b.factions == GearFactions::Both || a.factions == b.factions;
        }

        // A hand-made template has at most one item per position at each
        // level, content patch, and faction, two for finger and trinket. The error is
        // on the first item that overflows a position, at its lowest level.
        std::string CheckHandPositions(std::vector<GearItem> const& items, std::size_t& badLine)
        {
            for (std::size_t i = 0; i < items.size(); ++i)
            {
                GearItem const& item = items[i];
                for (std::string const& position : CountedPositions(item.slot))
                {
                    for (std::uint32_t level = item.minLevel; level <= item.maxLevel; ++level)
                    {
                        std::size_t count = 0;
                        for (std::size_t j = 0; j <= i; ++j)
                        {
                            GearItem const& other = items[j];
                            if (other.minPatch == item.minPatch && SharesFaction(other, item) && other.minLevel <= level &&
                                level <= other.maxLevel && SharesPosition(other, position))
                                ++count;
                        }
                        if (count > Capacity(position))
                        {
                            badLine = item.line;
                            return std::string(count == 2 ? "Two" : "Three") + " items fill the position " + position +
                                " at level " + std::to_string(level) + " and content patch " + std::to_string(item.minPatch) + ".";
                        }
                    }
                }
            }
            return "";
        }
    }

    bool FitsFaction(GearItem const& item, GearFaction faction)
    {
        switch (item.factions)
        {
            case GearFactions::Horde: return faction == GearFaction::Horde;
            case GearFactions::Alliance: return faction == GearFaction::Alliance;
            case GearFactions::Both: break;
        }
        return true;
    }

    char const* GearSlotName(GearSlot slot)
    {
        for (SlotInfo const& info : Slots)
            if (info.slot == slot)
                return info.name;
        return "";
    }

    char const* GearPositionName(GearPosition position)
    {
        switch (position)
        {
            case GearPosition::Head: return "head";
            case GearPosition::Neck: return "neck";
            case GearPosition::Shoulders: return "shoulders";
            case GearPosition::Back: return "back";
            case GearPosition::Chest: return "chest";
            case GearPosition::Shirt: return "shirt";
            case GearPosition::Tabard: return "tabard";
            case GearPosition::Wrists: return "wrists";
            case GearPosition::Hands: return "hands";
            case GearPosition::Waist: return "waist";
            case GearPosition::Legs: return "legs";
            case GearPosition::Feet: return "feet";
            case GearPosition::Finger1: return "finger1";
            case GearPosition::Finger2: return "finger2";
            case GearPosition::Trinket1: return "trinket1";
            case GearPosition::Trinket2: return "trinket2";
            case GearPosition::Ranged: return "ranged";
            case GearPosition::MainHand: return "mainhand";
            case GearPosition::OffHand: break;
        }
        return "offhand";
    }

    std::string GeneratedGearTemplateName(BotClass botClass, BotRole role)
    {
        return std::string(ClassName(botClass)) + "_" + RoleName(role);
    }

    std::string CheckGearTemplateFileName(GearTemplate const& gearTemplate, std::string const& fileStem)
    {
        std::string const generatedName = GeneratedGearTemplateName(gearTemplate.botClass, gearTemplate.role);
        if (gearTemplate.kind == GearTemplateKind::Generated && gearTemplate.name != generatedName)
            return "A generated template of a " + std::string(ClassName(gearTemplate.botClass)) + " " +
                RoleName(gearTemplate.role) + " must be named " + generatedName + ".";
        if (gearTemplate.kind == GearTemplateKind::Hand && gearTemplate.name == generatedName)
            return "The name " + generatedName + " is for the generated template. Give the hand-made template another name.";
        if (gearTemplate.name != fileStem)
            return "The file of the template " + gearTemplate.name + " must be " + gearTemplate.name + ".txt.";
        return "";
    }

    GearTemplateResult ParseGearTemplate(std::string const& text)
    {
        std::vector<DataLine> const lines = TokenizeLines(text);
        if (lines.empty())
            return Fail(1, "The file has no template line.");

        GearTemplateResult result;
        result.ok = true;
        GearTemplate& gear = result.gearTemplate;
        bool const generated = lines[0].words[0] == "template" && lines[0].words.size() == 5 && lines[0].words[4] == "generated";

        for (std::size_t index = 0; index < lines.size(); ++index)
        {
            DataLine const& line = lines[index];
            std::vector<std::string> const& words = line.words;
            if (index == 0 && words[0] != "template")
                return Fail(line.number, "The first line must be the template line.");

            if (words[0] == "template")
            {
                if (index != 0)
                    return Fail(line.number, "The file has two template lines.");
                if (words.size() != 5)
                    return Fail(line.number, "Use: template <name> <class> <role> generated|hand");
                if (!IsValidName(words[1]))
                    return Fail(line.number, "A template name has only a-z, 0-9, _ and -.");
                if (!ParseClassName(words[2], gear.botClass))
                    return Fail(line.number, "Unknown Horde class '" + words[2] + "'.");
                if (!ParseRoleName(words[3], gear.role))
                    return Fail(line.number, "Unknown role '" + words[3] + "'.");
                if (!ClassCanPlayRole(gear.botClass, gear.role))
                    return Fail(line.number, WithArticle(ClassName(gear.botClass)) + " cannot play the " + RoleName(gear.role) + " role.");
                if (words[4] != "generated" && words[4] != "hand")
                    return Fail(line.number, "The kind must be generated or hand, not '" + words[4] + "'.");
                gear.name = words[1];
                gear.kind = generated ? GearTemplateKind::Generated : GearTemplateKind::Hand;
            }
            else if (words[0] == "shape")
            {
                if (!generated)
                    return Fail(line.number, "A hand-made template has no shapes: it has no draw.");
                if (words.size() < 3 || words.size() > 4)
                    return Fail(line.number, "Use: shape <name> <weaponSlot> [<weaponSlot>]");
                WeaponShape shape;
                shape.name = words[1];
                for (WeaponShape const& other : gear.shapes)
                    if (other.name == shape.name)
                        return Fail(line.number, "Two shapes are named '" + shape.name + "'.");
                int mainHands = 0;
                int offHands = 0;
                for (std::size_t i = 2; i < words.size(); ++i)
                {
                    GearSlot slot;
                    if (!ParseSlot(words[i], slot) || !IsWeaponSlot(slot))
                        return Fail(line.number, "'" + words[i] + "' is not a weapon slot. Use mainhand, offhand, twohand, shield, or holdable.");
                    mainHands += FillsMainHand(slot) ? 1 : 0;
                    offHands += FillsOffHand(slot) ? 1 : 0;
                    shape.slots.push_back(slot);
                }
                if (mainHands > 1 || offHands > 1)
                    return Fail(line.number, "The slots of a shape fill the main hand and the off hand once each.");
                gear.shapes.push_back(shape);
            }
            else if (words[0] == "item")
            {
                std::size_t const fields = generated ? 6 : 5;
                double ignored = 0.0;
                bool const handScore = !generated && words.size() == fields + 1 && ParseScore(words[fields], ignored);
                if (generated && words.size() != fields && words.size() != fields + 1)
                    return Fail(line.number, "Use: item <slot> <entry> <minLevel>-<maxLevel> <minPatch> <score> [horde|alliance]");
                if (!generated && ((words.size() != fields && words.size() != fields + 1) || handScore))
                    return Fail(line.number, "Use: item <slot> <entry> <minLevel>-<maxLevel> <minPatch> [horde|alliance] (a hand-made item has no score)");
                GearItem item;
                item.line = line.number;
                item.score = 0.0;
                item.factions = GearFactions::Both;
                if (!ParseSlot(words[1], item.slot))
                    return Fail(line.number, "Unknown slot '" + words[1] + "'.");
                if (!ParseNumber(words[2], item.entry) || item.entry == 0)
                    return Fail(line.number, "The entry '" + words[2] + "' is not a number above 0.");
                if (!ParseLevels(words[3], item.minLevel, item.maxLevel) || item.minLevel < 1 ||
                    item.minLevel > item.maxLevel || item.maxLevel > MaxLevel)
                    return Fail(line.number, "The levels '" + words[3] + "' are not <min>-<max> from 1 to 60.");
                if (!ParseNumber(words[4], item.minPatch) || item.minPatch > MaxPatch)
                    return Fail(line.number, "The content patch '" + words[4] + "' is not a number from 0 to 10.");
                if (generated && (!ParseScore(words[5], item.score) || !(item.score > 0.0)))
                    return Fail(line.number, "The score '" + words[5] + "' is not a number above 0.");
                if (words.size() == fields + 1)
                {
                    std::string const& faction = words[fields];
                    if (faction == "horde")
                        item.factions = GearFactions::Horde;
                    else if (faction == "alliance")
                        item.factions = GearFactions::Alliance;
                    else
                        return Fail(line.number, "The faction '" + faction +
                            "' is not horde or alliance. A line for both factions has no faction word.");
                }
                gear.items.push_back(item);
            }
            else
                return Fail(line.number, "Unknown line '" + words[0] + "'. Use template, shape, or item.");
        }

        if (generated)
        {
            for (GearItem const& item : gear.items)
            {
                if (!IsWeaponSlot(item.slot))
                    continue;
                bool inShape = false;
                for (WeaponShape const& shape : gear.shapes)
                    for (GearSlot slot : shape.slots)
                        inShape = inShape || slot == item.slot;
                if (!inShape)
                    return Fail(item.line, "The slot " + std::string(GearSlotName(item.slot)) + " is in no shape line.");
            }
        }
        else
        {
            std::size_t badLine = 0;
            std::string const error = CheckHandPositions(gear.items, badLine);
            if (!error.empty())
                return Fail(badLine, error);
        }
        return result;
    }
}
