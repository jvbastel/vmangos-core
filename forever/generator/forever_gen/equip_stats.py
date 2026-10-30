"""The item bonuses of the spec (#227): every item stat other than the 1.12 base stats becomes a stock 1.12
equip spell.

A 1.12 item has no hit, crit, spell power or other rating stat. A stock item gives such a stat with an equip
spell, for example 7597 "Increased Critical 1" for 1% critical strike chance. The spec names a bonus as
"<name> <value>" in the `bonuses` column of items.tsv, and the generator puts the stock spell of that value
on the item.

A Forever value with no stock spell (for example 1.5% crit) is rounded to the nearest value that has one,
up on a tie. The spec writes it as "<name> <rounded> (Forever <value>)", so that the Forever value stays
next to the rounded one; the generator refuses a rounded value that is not the nearest one.

BONUSES holds, per bonus, the spell effects of its spells and the stock spell of each value. The spell of a
value is the one that most stock items use for it (the item_template dump of the stock world database), then
the lowest ID. A value that no stock item uses, up to the largest one that an item uses, takes the lowest
equip-style spell (passive, no duration, no class family, no equipped item, spell level 0) that gives it,
for example 9137 Attack Power 04. Values of GM-only items (1000) are left out. `check` proves each spell
against the stock Spell.dbc, so a wrong table entry fails the generator.

The bonus names: Crit, Hit, Dodge, Parry and Block in percent; BlockValue; Defense (skill); AP (melee and
ranged attack power) and RangedAP; SpellPower (damage and healing of all magic schools) and Healing;
SpellCrit and SpellHit in percent; Mp5 (mana every 5 seconds); AllRes (all magic resistances); and the
spell damage of one school, HolyDamage to ArcaneDamage.
"""
from collections import namedtuple

from .common import NO_VALUE, SPELL_BASE_POINTS1, SPELL_DIE_SIDES1, SPELL_EFFECT1, SpecError, int32

# name: the spec name; value: the 1.12 value (a whole number); forever: the Forever value of a rounded
# bonus, else None; spell: the stock equip spell.
Bonus = namedtuple("Bonus", "name value forever spell")

# name: ((aura, misc value) of each spell effect, {value: stock spell}). Every effect of the spell is
# "apply aura" with this aura and misc value and the bonus value as its points.
BONUSES = {
    "Crit": (((52, 0),), {
        1: 7597, 2: 7598, 3: 7599, 4: 4435, 5: 9405, 6: 4437, 8: 16945, 10: 17713
    }),
    "Hit": (((54, 0),), {
        1: 15464, 2: 15465, 3: 15466, 4: 15467, 5: 15468, 10: 18066
    }),
    "Dodge": (((49, 0),), {
        1: 13669, 2: 13670, 3: 13671, 4: 13672, 5: 13673, 8: 18062
    }),
    "Parry": (((47, 0),), {
        1: 13665, 2: 13667, 3: 13668, 4: 13855, 5: 18063
    }),
    "Block": (((51, 0),), {
        1: 13674, 2: 13675, 3: 21475, 4: 13677, 5: 21485, 6: 13678, 7: 21490, 8: 21494, 20: 18064
    }),
    "BlockValue": (((158, 0),), {
        1: 21539, 10: 23172, 12: 25036, 14: 23515, 15: 23203, 18: 22912, 19: 23731, 21: 28112, 23: 22852, 24:
        23181, 27: 23516, 30: 23562, 32: 28325
    }),
    "Defense": (((30, 95),), {
        1: 7427, 2: 7514, 3: 7516, 4: 7517, 5: 13383, 6: 13384, 7: 13385, 8: 13387, 9: 13388, 10: 13390, 11:
        18185, 12: 21408, 13: 14249, 14: 21410, 15: 18196, 16: 21412, 17: 13389, 18: 21413, 19: 21414, 20:
        21416, 21: 17513, 22: 21418, 23: 21419, 24: 21421, 25: 21422, 67: 15804, 80: 24775, 120: 24774
    }),
    "AP": (((99, 0), (124, 0)), {
        2: 9136, 4: 9137, 6: 9138, 8: 9139, 10: 9140, 12: 9141, 14: 9142, 15: 20732, 16: 9329, 18: 9330, 20:
        9331, 22: 9332, 24: 14027, 26: 9334, 28: 9335, 30: 9336, 32: 15807, 34: 15806, 36: 14089, 38: 15808, 40:
        14049, 42: 15809, 44: 15810, 48: 9333, 50: 14056, 52: 15812, 54: 15813, 56: 15814, 58: 15815, 60: 14052,
        62: 15816, 64: 15817, 66: 15818, 68: 15819, 70: 15820, 72: 15821, 74: 15823, 76: 15824, 78: 15825, 80:
        15826, 82: 15827, 84: 15828, 86: 15829, 88: 15830, 90: 15831, 92: 15832, 98: 28840
    }),
    "RangedAP": (((124, 0),), {
        2: 21426, 5: 21427, 7: 21428, 10: 21429, 12: 21430, 14: 21431, 17: 21432, 19: 21433, 22: 21434, 24:
        21435, 26: 21436, 29: 21437, 31: 21438, 34: 21439, 36: 21440, 38: 21441, 41: 21442, 43: 21443, 46:
        21444, 48: 21445
    }),
    "SpellPower": (((13, 126), (135, 126)), {
        1: 9392, 2: 9393, 4: 9394, 5: 9395, 6: 9396, 7: 9397, 8: 9398, 9: 9415, 11: 9416, 12: 9417, 13: 9342,
        14: 9343, 15: 9344, 16: 9345, 18: 9346, 19: 14254, 20: 14799, 21: 14248, 22: 15714, 23: 14047, 24:
        24595, 25: 15715, 26: 18049, 27: 14054, 28: 14127, 29: 13881, 30: 14798, 32: 17367, 33: 18050, 34:
        18052, 35: 14055, 36: 18053, 37: 18054, 39: 18055, 40: 18056, 41: 18057, 42: 18058, 43: 17280, 44:
        17493, 46: 28264, 47: 24196, 49: 28360, 51: 28767, 53: 26142, 56: 23732, 57: 23213, 59: 26155, 60:
        26158, 64: 23730, 68: 26227, 71: 23929, 72: 26395, 76: 26460, 80: 26459, 84: 23728, 85: 28687, 95:
        28693, 113: 28841, 126: 28792, 132: 28530, 150: 28141
    }),
    "Healing": (((135, 126),), {
        2: 7675, 4: 7676, 7: 7677, 9: 7678, 11: 7679, 13: 7680, 15: 7681, 18: 9406, 20: 9407, 22: 9408, 24:
        9314, 26: 9315, 29: 9316, 30: 25067, 31: 9317, 33: 9318, 35: 18029, 37: 18030, 40: 18031, 42: 18032, 44:
        17371, 46: 18033, 48: 18034, 51: 18035, 53: 15696, 55: 18036, 57: 18037, 59: 18038, 62: 18039, 64:
        18040, 66: 18041, 68: 18042, 70: 18043, 73: 18044, 75: 18045, 77: 18046, 79: 18047, 81: 18048, 84:
        17320, 90: 26154, 92: 23593, 106: 23264, 110: 26690, 121: 26228, 134: 29369, 143: 26461, 161: 28686,
        178: 28736, 187: 26814, 238: 28805, 280: 28151, 300: 28152
    }),
    "SpellCrit": (((71, 126),), {
        1: 18384, 2: 18382, 50: 23433
    }),
    "SpellHit": (((55, 0),), {
        1: 23727, 2: 23729
    }),
    "Mp5": (((85, 0),), {
        1: 21359, 2: 21624, 3: 21625, 4: 21618, 5: 21363, 6: 21626, 7: 21364, 8: 18378, 9: 21365, 10: 21632, 11:
        21634, 12: 21636, 13: 21638, 14: 21640, 15: 21643, 16: 23212
    }),
    "AllRes": (((22, 126),), {
        1: 7438, 2: 18673, 3: 6443, 4: 18675, 5: 18676
    }),
    "HolyDamage": (((13, 2),), {
        1: 21499, 2: 8474, 3: 8475, 4: 21501, 5: 8476, 6: 8477, 7: 21503, 8: 8478, 9: 9309, 10: 21505, 11: 9310,
        12: 9311, 13: 21507, 14: 9312, 15: 9313, 16: 21509, 17: 21510, 19: 21511, 20: 21512, 21: 21513, 23:
        21514, 24: 21515, 26: 21516, 27: 21517, 29: 21518
    }),
    "FireDamage": (((13, 4),), {
        1: 7683, 2: 8469, 3: 7684, 4: 7685, 5: 8430, 6: 7686, 7: 7687, 8: 8755, 9: 7688, 10: 7689, 11: 8757, 12:
        9076, 13: 9400, 14: 9401, 15: 9293, 16: 9294, 17: 9295, 19: 9296, 20: 9297, 21: 9298, 23: 17747, 24:
        17866, 26: 17867, 27: 17868, 29: 13830, 30: 17869, 31: 17870, 33: 17871, 34: 17872, 36: 17873, 37:
        17874, 39: 17684, 40: 17875, 41: 17876, 43: 17878, 44: 17879, 46: 17880, 47: 17881, 49: 17882, 50:
        17884, 51: 17885, 53: 17886, 54: 17887, 57: 26405
    }),
    "NatureDamage": (((13, 8),), {
        1: 7690, 3: 7691, 4: 7692, 5: 9349, 6: 7693, 7: 7694, 8: 9351, 9: 7695, 10: 7696, 11: 9353, 12: 9354,
        14: 9411, 15: 9356, 16: 9357, 17: 9358, 19: 9359, 20: 9360, 21: 9361, 23: 17987, 24: 17988, 26: 17989,
        27: 17990, 29: 17819, 30: 17991, 33: 17993, 34: 16638, 36: 17994, 37: 17995, 39: 17996, 40: 17997
    }),
    "FrostDamage": (((13, 16),), {
        1: 7697, 2: 8741, 3: 7698, 4: 7699, 5: 8743, 6: 7700, 7: 7701, 8: 8745, 9: 7702, 10: 7703, 11: 9402, 12:
        9301, 13: 9403, 14: 9404, 15: 9303, 16: 9304, 17: 9305, 19: 9306, 20: 9307, 21: 9308, 23: 17889, 24:
        17890, 26: 17891, 27: 17892, 29: 13831, 30: 17893, 31: 17894, 33: 17895, 34: 17896, 36: 17897, 37:
        17898, 39: 17899, 40: 17900, 41: 17901, 43: 17902, 44: 17903, 46: 17905, 47: 17906, 49: 17907, 50:
        17908, 51: 17909, 54: 17911, 56: 17910, 57: 26717, 59: 26718, 60: 26719, 61: 26720, 63: 26721, 64:
        26722, 66: 26723, 67: 26724, 69: 26725, 70: 26726, 72: 24667, 132: 24666
    }),
    "ShadowDamage": (((13, 32),), {
        1: 7704, 2: 8432, 3: 7705, 4: 7706, 5: 8471, 6: 7707, 7: 7708, 8: 8473, 9: 7710, 10: 7709, 11: 9412, 12:
        9321, 13: 9413, 14: 9414, 15: 9323, 16: 9324, 17: 9325, 19: 9326, 20: 9327, 21: 9328, 23: 14793, 24:
        14794, 26: 18008, 27: 18009, 29: 18010, 30: 18011, 31: 18012, 33: 18013, 34: 18014, 36: 18015, 37:
        18016, 39: 18017, 40: 18018, 41: 18019, 43: 18020, 44: 18021, 46: 18022, 47: 18023, 49: 18024, 50:
        18025, 51: 18026, 53: 18027, 54: 18028, 56: 26728, 57: 26729, 59: 26730, 60: 26731, 61: 26732, 63:
        26733, 64: 23594, 66: 26734, 67: 26735, 69: 23265
    }),
    "ArcaneDamage": (((13, 64),), {
        1: 13590, 3: 13591, 4: 13592, 6: 13593, 7: 13594, 9: 13595, 10: 13596, 11: 13597, 13: 13598, 14: 13599,
        16: 13601, 17: 13602, 19: 13603, 20: 13604, 21: 13605, 23: 17821, 26: 17823, 27: 17824, 29: 17825, 30:
        17826, 31: 17827, 33: 17828, 34: 17829, 36: 17830
    }),
}

# Spell.dbc 1.12.1: 61-63 Effect, 64-66 EffectDieSides, 76-78 EffectBasePoints (common.py), 91-93
# EffectApplyAuraName, 106-108 EffectMiscValue.
EFFECT, DIE_SIDES, BASE_POINTS = SPELL_EFFECT1, SPELL_DIE_SIDES1, SPELL_BASE_POINTS1
AURA, MISC = 91, 106
SPELL_EFFECT_APPLY_AURA = 6


def nearest(name, value, table=BONUSES):
    """The value of the bonus `name` nearest to `value` that has a stock spell; up on a tie."""
    return min(table[name][1], key=lambda v: (abs(v - value), -v))


def parse(where, text, table=BONUSES):
    """The bonuses of a `bonuses` value: "<name> <value>" or "<name> <value> (Forever <value>)", separated
    by commas, or "-". Refuses a name with no stock equip spell, a value with none and no Forever value
    (the rounding rule), and a rounded value that is not the nearest one. `table`: the bonus table (the item
    sets of #231 add bonuses of their own)."""
    if text == NO_VALUE:
        return []
    bonuses = []
    for part in text.split(","):
        part = part.strip()
        head, _, forever = part.partition(" (Forever ")
        name, _, value = head.partition(" ")
        if name not in table:
            raise SpecError("%s: the bonus '%s' has no stock equip spell (bonuses: %s)" % (where, part, ", ".join(table)))
        if not value.isdigit() or (forever and not forever.endswith(")")):
            raise SpecError("%s: the bonus '%s' is not '<name> <value>' or '<name> <value> (Forever <value>)'" % (where, part))
        value, spells = int(value), table[name][1]
        if not forever:
            if value not in spells:
                raise SpecError("%s: no stock equip spell gives %s %d, and the bonus has no Forever value to round "
                                "(write '%s %d (Forever %d)')" % (where, name, value, name, nearest(name, value, table), value))
            bonuses.append(Bonus(name, value, None, spells[value]))
            continue
        try:
            exact = float(forever[:-1])
        except ValueError:
            raise SpecError("%s: the Forever value of '%s' is no number" % (where, part))
        if value != nearest(name, exact, table):
            raise SpecError("%s: %s %s rounds to %d, not %d" % (where, name, forever[:-1], nearest(name, exact, table), value))
        bonuses.append(Bonus(name, value, exact, spells[value]))
    return bonuses


def check(stock, table=BONUSES):
    """Proves the table against the stock Spell.dbc: each spell exists, and each of its effects applies the
    aura of its bonus with the bonus value."""
    spells = stock.spell.by_id()
    for name, (auras, values) in table.items():
        for value, spell in values.items():
            row = spells.get(spell)
            effects = [] if row is None else [
                (row[AURA + k], row[MISC + k], int32(row[BASE_POINTS + k]) + row[DIE_SIDES + k])
                for k in range(3) if row[EFFECT + k]]
            if row is None or [e[:2] for e in effects] != list(auras) or \
                    any(row[EFFECT + k] not in (0, SPELL_EFFECT_APPLY_AURA) for k in range(3)) or \
                    any(e[2] != value for e in effects):
                raise SpecError("the equip spell table: spell %d is no stock spell that gives %s %d" % (spell, name, value))
