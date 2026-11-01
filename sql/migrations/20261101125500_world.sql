DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261101125500');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261101125500');
-- Add your query below.

-- WOWAI: the hardcore NPC in all starting areas and capitals, and the drop of the status (spec
-- jvbastel/WOWAI#250, WOWAI#255). The NPC (creature 190250, script npc_hardcore) and its Valley of Trials
-- spawn come from 20261101120100_world.sql. This adds its other 11 spawns (guids 2502501 to 2502511) and the
-- texts of the drop. The script tells a capital by the zone of the NPC: there it shows the capital greeting
-- and only offers the drop.
--
-- Each place comes from the world database (2026-11-01), not from memory: a starting area spawn stands about
-- 3 yards from a stock NPC next to the start point of playercreateinfo; a capital spawn stands in the bank,
-- about 5 yards in front of a stock banker (Undercity: just outside the ring of bankers), at the banker's z.

DELETE FROM `broadcast_text` WHERE `entry` IN (190252, 190253);
INSERT INTO `broadcast_text` (`entry`, `male_text`, `female_text`) VALUES
(190252, 'Greetings, $n. I keep the hardcore status. In the city I can only take it away: a hardcore character can drop it here and become a normal character. A new character takes the status in its starting area.', 'Greetings, $n. I keep the hardcore status. In the city I can only take it away: a hardcore character can drop it here and become a normal character. A new character takes the status in its starting area.'),
(190253, 'Hear me well, $n.$B$BIf you drop the hardcore status, this character becomes a normal character. A dropped status never comes back: no NPC and no GM can give it to this character again.$B$BEvery hardcore bot of your group drops the status with you. A hardcore bot that is not in your group now keeps it, and this character can never invite it again.$B$BDo you drop the status?', 'Hear me well, $n.$B$BIf you drop the hardcore status, this character becomes a normal character. A dropped status never comes back: no NPC and no GM can give it to this character again.$B$BEvery hardcore bot of your group drops the status with you. A hardcore bot that is not in your group now keeps it, and this character can never invite it again.$B$BDo you drop the status?');

DELETE FROM `npc_text` WHERE `ID` IN (190252, 190253);
INSERT INTO `npc_text` (`ID`, `BroadcastTextID0`, `Probability0`) VALUES
(190252, 190252, 1),
(190253, 190253, 1);

DELETE FROM `creature` WHERE `guid` BETWEEN 2502501 AND 2502511;
INSERT INTO `creature` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`,
  `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`) VALUES
-- Starting areas (start point of playercreateinfo; the stock NPC next to it).
-- Northshire (human; -8949.95 -132.49): next to Deputy Willem (guid 79942), toward the start point.
(2502501, 190250, 0, -8936.9, -135.7, 83.45, 2.90, 300, 300, 0, 0),
-- Coldridge Valley (dwarf, gnome; -6240.32 331.03): next to Yori Crackhelm (guid 348), toward the start point.
(2502502, 190250, 0, -6232.8, 323.0, 383.14, 2.32, 300, 300, 0, 0),
-- Shadowglen (night elf; 10311.3 831.46): next to Conservator Ilthalaine (guid 46205), toward the start point.
(2502503, 190250, 1, 10325.6, 827.1, 1326.46, 2.84, 300, 300, 0, 0),
-- Deathknell (undead; 1676.35 1677.45, in the crypt): next to Undertaker Mordo (guid 29803) at the crypt.
(2502504, 190250, 0, 1678.6, 1664.9, 135.85, 3.77, 300, 300, 0, 0),
-- Camp Narache (tauren; -2917.58 -257.98): beside Grull Hawkwind (guid 26558).
(2502505, 190250, 1, -2912.6, -261.0, 53.02, 3.18, 300, 300, 0, 0),
-- Capitals (the bank).
-- Stormwind City: in front of John Burnside (guid 12652).
(2502506, 190250, 0, -8934.9, 622.9, 99.61, 0.51, 300, 300, 0, 0),
-- Ironforge, The Vault: in front of Barnum Stonemantle (guid 12632).
(2502507, 190250, 0, -4880.7, -986.3, 504.02, 2.30, 300, 300, 0, 0),
-- Darnassus: in front of Lairn (guid 12648).
(2502508, 190250, 1, 9942.6, 2515.4, 1317.66, 4.00, 300, 300, 0, 0),
-- Orgrimmar: in front of Koma (guid 12633).
(2502509, 190250, 1, 1627.9, -4384.1, 11.80, 3.58, 300, 300, 0, 0),
-- Thunder Bluff: in the bank, the open side of Atepa, Torn and Chesmu (guids 12641, 12642, 12640).
(2502510, 190250, 1, -1257.6, 27.0, 128.27, 1.57, 300, 300, 0, 0),
-- Undercity: outside the ring of bankers, between William and Ophelia Montague (guids 12644, 12646).
(2502511, 190250, 0, 1591.9, 244.2, -52.06, 2.36, 300, 300, 0, 0);

-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
