DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261029120000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261029120000');
-- Add your query below.

-- WOWAI: the Forever server tables (spec jvbastel/WOWAI#204). Each has the shape of its stock table.
-- forever/generator fills them from forever/warrior.tsv. The core reads them only with
-- Forever.Rules = 1, on top of the stock rows by ID. Empty, or with the setting off, they change
-- nothing.
CREATE TABLE IF NOT EXISTS `forever_spell_template` LIKE `spell_template`;
CREATE TABLE IF NOT EXISTS `forever_spell_chain` LIKE `spell_chain`;
CREATE TABLE IF NOT EXISTS `forever_spell_proc_event` LIKE `spell_proc_event`;
CREATE TABLE IF NOT EXISTS `forever_skill_line_ability` LIKE `skill_line_ability`;
-- The baseline spells (#212): the core teaches each to a Warrior at its spellLevel, with no trainer.
-- forever/generator makes this table again with the same columns (build.py, _server_baseline; its test
-- test_spell_ids.py keeps the two the same).
CREATE TABLE IF NOT EXISTS `forever_baseline_spell` (`spell_id` SMALLINT UNSIGNED NOT NULL, PRIMARY KEY (`spell_id`));

-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
