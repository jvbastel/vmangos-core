// Checks the gear templates in bots/data/gear/: each file reads without an
// error, and the generated templates of the version 1 classes and roles are
// there. The game layer checks the item entries against item_template.
#include "GearDraw.h"
#include "GearTemplate.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace
{
    std::string const GearDir = std::string(BOT_DATA_DIR) + "/gear";

    std::string ReadFile(std::string const& path)
    {
        std::ifstream file(path, std::ios::binary);
        std::ostringstream text;
        text << file.rdbuf();
        return text.str();
    }

    Bots::GearTemplateResult ReadTemplate(std::string const& name)
    {
        return Bots::ParseGearTemplate(ReadFile(GearDir + "/" + name + ".txt"));
    }

    std::set<Bots::GearPosition> Positions(Bots::GearDraw const& draw)
    {
        std::set<Bots::GearPosition> positions;
        for (Bots::GearPick const& pick : draw.picks)
            positions.insert(pick.position);
        return positions;
    }
}

TEST(GearData, EveryGearTemplateFileReads)
{
    std::size_t files = 0;
    for (auto const& entry : std::filesystem::directory_iterator(GearDir))
    {
        if (entry.path().extension() != ".txt")
            continue;
        ++files;
        std::string const stem = entry.path().stem().string();
        Bots::GearTemplateResult const result = Bots::ParseGearTemplate(ReadFile(entry.path().string()));
        ASSERT_TRUE(result.ok) << entry.path() << ": " << result.error;
        EXPECT_EQ("", Bots::CheckGearTemplateFileName(result.gearTemplate, stem)) << entry.path();
    }
    EXPECT_GT(files, 0u);
}

TEST(GearData, TheVersion1ClassesAndRolesHaveGeneratedTemplates)
{
    for (char const* name : {"warrior_tank", "priest_healer", "rogue_melee", "warlock_ranged", "mage_ranged"})
    {
        Bots::GearTemplateResult const result = ReadTemplate(name);
        ASSERT_TRUE(result.ok) << name << ": " << result.error;
        EXPECT_EQ(Bots::GearTemplateKind::Generated, result.gearTemplate.kind) << name;
        EXPECT_FALSE(result.gearTemplate.shapes.empty()) << name;
    }
}

TEST(GearData, ALevel16BotOfTheVersion1GroupGetsArmorAndWeapons)
{
    // Content patch 0 (1.2), the patch of the play realm.
    for (char const* name : {"warrior_tank", "priest_healer", "rogue_melee", "warlock_ranged", "mage_ranged"})
    {
        Bots::GearTemplateResult const result = ReadTemplate(name);
        ASSERT_TRUE(result.ok) << name;
        std::set<Bots::GearPosition> const worn = Positions(Bots::DrawGear(result.gearTemplate, 42, 16, 0, Bots::GearFaction::Horde));
        for (Bots::GearPosition position : {Bots::GearPosition::Chest, Bots::GearPosition::Legs,
                 Bots::GearPosition::Feet, Bots::GearPosition::Hands, Bots::GearPosition::MainHand,
                 Bots::GearPosition::Ranged})
            EXPECT_TRUE(worn.count(position)) << name << " wears nothing at " << Bots::GearPositionName(position);
    }
}

TEST(GearData, TheWarriorTankWearsAShieldAndTheRogueTwoWeapons)
{
    Bots::GearTemplateResult const warrior = ReadTemplate("warrior_tank");
    Bots::GearTemplateResult const rogue = ReadTemplate("rogue_melee");
    ASSERT_TRUE(warrior.ok && rogue.ok);
    for (std::uint64_t guid = 1; guid <= 20; ++guid)
    {
        Bots::GearDraw const tank = Bots::DrawGear(warrior.gearTemplate, guid, 16, 0, Bots::GearFaction::Horde);
        EXPECT_EQ("sword-and-board", tank.shape);
        for (Bots::GearPick const& pick : tank.picks)
            if (pick.position == Bots::GearPosition::OffHand)
                EXPECT_EQ(Bots::GearSlot::Shield, pick.slot);
        Bots::GearDraw const melee = Bots::DrawGear(rogue.gearTemplate, guid, 16, 0, Bots::GearFaction::Horde);
        EXPECT_TRUE(Positions(melee).count(Bots::GearPosition::OffHand)) << "guid " << guid;
    }
}

TEST(GearData, AWarriorTankWearsARangedWeaponAtEveryLevel)
{
    // The tank pulls with it (WarriorModule.h). Patch 0 (1.2) is the patch
    // of the realms; the last patch is checked too.
    Bots::GearTemplateResult const result = ReadTemplate("warrior_tank");
    ASSERT_TRUE(result.ok) << result.error;
    for (std::uint32_t patch : {0u, 10u})
        for (std::uint32_t level = 1; level <= 60; ++level)
            EXPECT_TRUE(Positions(Bots::DrawGear(result.gearTemplate, 42, level, patch, Bots::GearFaction::Horde))
                    .count(Bots::GearPosition::Ranged))
                << "no ranged weapon at level " << level << ", patch " << patch;
}
