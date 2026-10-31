DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261030122800');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261030122800');
-- Add your query below.

-- WOWAI: the Forever trainer override table (jvbastel/WOWAI#228, spec #225), in the shape of npc_trainer.
-- forever/generator fills it from the stock rows of forever/spec/blacksmithing.tsv: a copy of every stock
-- npc_trainer row of a changed recipe's learn spell, with the Forever reqskillvalue. The core reads it only
-- with Forever.Rules = 1: its rows replace the stock npc_trainer rows of the same spell. Empty, or with the
-- setting off, it changes nothing.
CREATE TABLE IF NOT EXISTS `forever_npc_trainer` LIKE `npc_trainer`;

-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
