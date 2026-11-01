DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261031120000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261031120000');
-- Add your query below.

-- WOWAI: the Forever dungeon loot override tables (jvbastel/WOWAI#242, spec #240), in the shape of their stock
-- tables. forever_world.sql makes them again and adds the removal marker of #229. forever/generator fills them from
-- forever/spec/loot_*.tsv: a copy of every stock row of a boss's creature loot entry or reference loot entry plus
-- the new Forever drops, and a copy of every stock row of a quest with the Forever reward choices. The core reads them
-- only with Forever.Rules = 1: their rows replace the stock rows of the same entry. Empty, or with the setting off,
-- they change nothing. With the setting on and a table missing, mangosd stops at startup.
CREATE TABLE IF NOT EXISTS `forever_creature_loot_template` LIKE `creature_loot_template`;
CREATE TABLE IF NOT EXISTS `forever_reference_loot_template` LIKE `reference_loot_template`;
CREATE TABLE IF NOT EXISTS `forever_quest_template` LIKE `quest_template`;

-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
