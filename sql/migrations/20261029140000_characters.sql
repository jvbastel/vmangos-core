DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261029140000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261029140000');
-- Add your query below.

-- WOWAI: the rules that each character last logged in under (spec jvbastel/WOWAI#204, WOWAI#208).
-- The core writes the row at login. A character with no row has not logged in since this
-- migration, so it last played under the vanilla rules.
CREATE TABLE IF NOT EXISTS `forever_character` (
  `guid` INT UNSIGNED NOT NULL COMMENT 'characters.guid',
  `rules` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 vanilla rules, 1 Forever rules (Forever.Rules)',
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='WOWAI Forever rules of each character';

-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
