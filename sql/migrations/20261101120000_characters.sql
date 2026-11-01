DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261101120000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261101120000');
-- Add your query below.

-- WOWAI: the hardcore state of each character (spec jvbastel/WOWAI#250, WOWAI#251). One row per character
-- that ever took the hardcore status; a character with no row was never hardcore. src/game/Hardcore writes
-- each change at once, not with the 15-minute save. The death record columns stay 0 or empty until the
-- character dies (WOWAI#253).
CREATE TABLE IF NOT EXISTS `character_hardcore` (
  `guid` INT UNSIGNED NOT NULL COMMENT 'characters.guid',
  `status` TINYINT UNSIGNED NOT NULL DEFAULT 1 COMMENT '1 hardcore (alive), 2 dead (final), 3 dropped (was hardcore)',
  `taken_at` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'unix time the character took the status',
  `dropped_at` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'unix time the character dropped the status, 0 if not',
  `death_time` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'death record: unix time of the death, 0 if alive',
  `death_zone` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'death record: zone id (AreaTable.dbc) of the death',
  `death_killer` VARCHAR(64) NOT NULL DEFAULT '' COMMENT 'death record: name of the killer, or the cause (fall, drowning, ...)',
  `death_level` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'death record: level of the character at the death',
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='WOWAI hardcore characters';

-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
