DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261030120000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261030120000');
-- Add your query below.

-- WOWAI: the Forever item override table (jvbastel/WOWAI#226, spec #225), in the shape of item_template.
-- forever/generator fills it from forever/spec/items.tsv. The core reads it only with Forever.Rules = 1: its
-- rows replace the stock item_template rows of the same entry, or add new items. Empty, or with the setting
-- off, it changes nothing.
CREATE TABLE IF NOT EXISTS `forever_item_template` LIKE `item_template`;

-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
