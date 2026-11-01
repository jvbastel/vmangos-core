DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261101120100');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261101120100');
-- Add your query below.

-- WOWAI: the hardcore NPC (spec jvbastel/WOWAI#250, WOWAI#251). One creature template with the gossip
-- script npc_hardcore (src/scripts/custom/npc_hardcore.cpp), its two gossip texts, and its spawn in Valley
-- of Trials. Faction 35 is friendly to both factions, so the same template can stand in each starting area
-- and capital (WOWAI#255 adds those spawns, guids 2502501 to 2502511, and its texts from 190252).
-- Free ids: creature entries end at 18199, broadcast_text at 100003, npc_text at 50009, creature guids at
-- 303722 (2026-11-01).

DELETE FROM `broadcast_text` WHERE `entry` IN (190250, 190251);
INSERT INTO `broadcast_text` (`entry`, `male_text`, `female_text`) VALUES
(190250, 'Greetings, $n. I keep the hardcore status. Only a new character can take it: level 1, with no experience, and never hardcore before.', 'Greetings, $n. I keep the hardcore status. Only a new character can take it: level 1, with no experience, and never hardcore before.'),
(190251, 'Hear the rules of a hardcore character, $n.$B$BYour first death is final. No spell, no spirit healer and no GM can bring you back, and the character can never log in again.$B$BYou get no help from characters that are not hardcore: no trade, no mail, no auction house and no group with them.$B$BYou can drop the status at a hardcore NPC, but a dropped status never comes back.$B$BDo you take the status?', 'Hear the rules of a hardcore character, $n.$B$BYour first death is final. No spell, no spirit healer and no GM can bring you back, and the character can never log in again.$B$BYou get no help from characters that are not hardcore: no trade, no mail, no auction house and no group with them.$B$BYou can drop the status at a hardcore NPC, but a dropped status never comes back.$B$BDo you take the status?');

DELETE FROM `npc_text` WHERE `ID` IN (190250, 190251);
INSERT INTO `npc_text` (`ID`, `BroadcastTextID0`, `Probability0`) VALUES
(190250, 190250, 1),
(190251, 190251, 1);

DELETE FROM `creature_template` WHERE `entry` = 190250;
INSERT INTO `creature_template` (`entry`, `patch`, `name`, `subname`, `level_min`, `level_max`, `faction`, `npc_flags`,
  `display_id1`, `display_scale1`, `display_probability1`, `display_total_probability`, `type`, `unit_class`,
  `static_flags1`, `flags_extra`, `script_name`) VALUES
(190250, 0, 'Keeper of Final Death', 'Hardcore status', 60, 60, 35, 1, 7052, 1, 100, 100, 7, 1, 134217734, 2, 'npc_hardcore');

-- Valley of Trials (Durotar), next to Kaltunk at the den, where a new orc or troll starts.
DELETE FROM `creature` WHERE `guid` = 2502500;
INSERT INTO `creature` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`,
  `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`) VALUES
(2502500, 190250, 1, -604.0, -4248.0, 39.0, 3.3, 300, 300, 0, 0);

-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
