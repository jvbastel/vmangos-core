DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261030140000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261030140000');
-- Add your query below.

-- WOWAI: the Forever gameobject loot override table (jvbastel/WOWAI#232, spec #225), in the shape of
-- gameobject_loot_template plus the removal marker of #229. forever/generator fills it from forever/spec/mining.tsv:
-- for each vein loot ID a copy of its stock rows plus the Forever ore (Pyrite, Bauxite, Pitchblende). The core reads
-- it only with Forever.Rules = 1: its rows replace the stock rows of the same loot ID (entry). Empty, or with the
-- setting off, it changes nothing. With the setting on and the table missing, mangosd stops at startup.
CREATE TABLE IF NOT EXISTS `forever_gameobject_loot_template` LIKE `gameobject_loot_template`;
ALTER TABLE `forever_gameobject_loot_template` ADD COLUMN IF NOT EXISTS `forever_removed` TINYINT UNSIGNED NOT NULL DEFAULT 0;

-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
