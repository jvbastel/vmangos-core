DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261024124046');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261024124046');
-- Add your query below.

-- WOWAI: the bots of the own bot system (src/game/Bots). Each row is one
-- persistent bot character on a bot pool account.
CREATE TABLE IF NOT EXISTS `bot_character` (
  `guid` INT UNSIGNED NOT NULL COMMENT 'characters.guid of the bot',
  `account` INT UNSIGNED NOT NULL COMMENT 'realmd account.id of the bot pool account',
  `role` VARCHAR(12) NOT NULL COMMENT 'tank, healer, melee, or ranged',
  `world_bot` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '1 for a world bot, 0 for a group bot',
  PRIMARY KEY (`guid`),
  KEY `idx_account` (`account`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='WOWAI bot characters';

-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
