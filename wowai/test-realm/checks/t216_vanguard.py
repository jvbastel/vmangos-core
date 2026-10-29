#!/usr/bin/env python3
"""In-game check of jvbastel/WOWAI#216: the Vanguard talent swaps Charge for Vanguard Charge, which can be
used in Defensive Stance (spec #204), on the check kit. Each step is one run on the same slot; an acquire
between them restarts the slot with the other rules and keeps the database (same lease):

    wowai/test-realm/realm.sh --ticket N acquire --forever
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t216_vanguard.py forever
    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t216_vanguard.py vanilla

forever (rules on) A level 50 player Warrior with Charge ranks 1 and 2 (100, 6178) and Defensive Stance
        puts Charge rank 2 on a button. In Defensive Stance, stock Charge fails with "Must be in Battle
        Stance". It takes 15 Protection points and Vanguard (talent 60007, spell 60119): the client gets
        Charge rank 2 superseded by Vanguard Charge rank 2 (60128), the button casts 60128, the stock ranks
        stay known but inactive. Vanguard Charge then charges a boar in Defensive Stance. Charge rank 3
        (11578, as from the trainer) gives Vanguard Charge rank 3 (60129) on the same button. A login
        keeps it. A talent reset gives back Charge rank 3 on the same button. Vanguard again, then
        logout.
vanilla (rules off) The switch to vanilla gives back stock Charge rank 3: in the spellbook (login packet,
        active in character_spell) and on the same button. No Vanguard Charge is left.
"""
import sys

from kit import Realm, Results, kill, pos, spawn, sturdy
from wowclient import MSG_MOVE_HEARTBEAT, SpellClient
SPELL_FAILED_ONLY_SHAPESHIFT = 86  # "Must be in %s" (1.12 SpellCastResult)

ACCOUNT = "PLAYER216"
NAME = "Vanguardwar"
LEVEL = 50
DEFENSIVE_STANCE = 71
CHARGE = (100, 6178, 11578)
VANGUARD_CHARGE = (60127, 60128, 60129)
VANGUARD = 60007
VANGUARD_SPELL = 60119
# 15 Protection points (forever/warrior.tsv): Shield Specialization 5, Anticipation 5, Improved Revenge 3,
# Improved Bloodrage 2. Vanguard is on tier 4.
PROTECTION_15 = ((1601, 5), (138, 5), (147, 3), (142, 2))
BUTTON_CHARGE = 115  # a high button, free on a new character
SWITCH = r"\[Forever\] %s switches from the Forever rules to the vanilla rules"


mode = sys.argv[1] if len(sys.argv) > 1 else ""
if mode not in ("forever", "vanilla"):
    sys.exit("usage: t216_vanguard.py forever|vanilla")

realm = Realm()
results = Results()
realm.mark("t216 " + mode)
c = realm.login(ACCOUNT, NAME, race=2, cls=1, client_class=SpellClient)
me = c.guid
c.pump(2)


def charge_rows():
    """spell -> active of the saved Charge and Vanguard Charge rows. A lower Charge rank that a higher rank
    taught (Player::AddSpell learns it as dependent) has no row: Player::_SaveSpells saves no dependent spell."""
    c.gm(".save", select=me)
    c.pump(1)
    rows = realm.sql("characters", "SELECT spell, active FROM character_spell WHERE guid = %d AND disabled = 0 "
                     "AND spell IN (%s)" % (me, ", ".join(map(str, CHARGE + VANGUARD_CHARGE))))
    return {int(s): int(a) for s, a in (row.split("\t") for row in rows.splitlines() if row)}


def charge_button():
    """The spell on BUTTON_CHARGE. The core saves the action bars only at logout (MasterPlayer::SaveToDB,
    not `.save`), so this logs out and in again first."""
    c.gm(".combatstop", select=me)
    c.relog()
    rows = realm.sql("characters", "SELECT action FROM character_action WHERE guid = %d AND type = 0 AND button = %d"
                     % (me, BUTTON_CHARGE))
    return int(rows) if rows else None


def learn_vanguard():
    for talent, ranks in PROTECTION_15:
        for rank in range(ranks):
            c.learn_talent(talent, rank)
    start = len(c.superseded)
    learned = c.learn_talent(VANGUARD)
    return learned, c.superseded[start:]


def spawn_boar_ahead():
    """A frozen boar 15 yards east of the player (Charge range 8-25), out of combat, and the player facing
    it (Charge needs the target in front: SPELL_FAILED_UNIT_NOT_INFRONT, 124). Its guid."""
    boar = spawn(realm, c, wait=2.0)
    sturdy(c, boar)
    x, y, z = pos(c, boar)
    c.gm(".go xyz %f %f %f" % (x - 15.0, y, z + 1.0), wait=2.0)
    x, y, z = pos(c, me)
    c.move(MSG_MOVE_HEARTBEAT, x, y, z, 0.0)  # face east, at the boar
    c.pump(1)
    return boar


if mode == "forever":
    level = int(realm.sql("characters", "SELECT level FROM characters WHERE guid = %d" % me) or 1)
    if level < LEVEL:
        c.gm(".levelup %d" % (LEVEL - level), select=me)
    c.gm(".reset talents", select=me)
    for spell in CHARGE + VANGUARD_CHARGE:  # a second run starts clean
        c.gm(".unlearn %d" % spell, select=me, wait=0.5)
    for spell in (DEFENSIVE_STANCE, CHARGE[0], CHARGE[1]):
        c.gm(".learn %d" % spell, select=me)
    c.set_button(BUTTON_CHARGE, CHARGE[1])
    rows = charge_rows()
    button = charge_button()
    results.check("Charge rank 2 is on the button", button == CHARGE[1], "button %r" % button)
    results.check("stock Charge rank 2 is active", rows.get(CHARGE[1]) == 1, "%r" % rows)

    c.cast(DEFENSIVE_STANCE)
    boar = spawn_boar_ahead()
    stock, _ = c.cast(CHARGE[1], boar)
    results.check("stock Charge fails in Defensive Stance (control)",
                  SPELL_FAILED_ONLY_SHAPESHIFT in stock, "reasons %r" % stock)

    learned, superseded = learn_vanguard()
    results.check("Vanguard is learned", VANGUARD_SPELL in learned, "learned %r" % learned)
    results.check("the client gets Charge rank 2 superseded by Vanguard Charge rank 2",
                  (CHARGE[1], VANGUARD_CHARGE[1]) in superseded, "superseded %r" % superseded)
    rows = charge_rows()
    results.check("Vanguard Charge rank 2 is active, the stock ranks inactive",
                  rows == {CHARGE[1]: 0, VANGUARD_CHARGE[1]: 1}, "%r" % rows)

    vanguard, go = c.cast(VANGUARD_CHARGE[1], boar)
    results.check("Vanguard Charge is not refused for the stance",
                  SPELL_FAILED_ONLY_SHAPESHIFT not in vanguard, "reasons %r" % vanguard)
    results.check("Vanguard Charge charges in Defensive Stance", go, "reasons %r" % vanguard)
    kill(c, boar)
    c.pump(2)
    button = charge_button()
    results.check("the button casts Vanguard Charge rank 2", button == VANGUARD_CHARGE[1],
                  "button %r" % button)

    start = len(c.superseded)
    c.gm(".learn %d" % CHARGE[2], select=me)
    results.check("Charge rank 3 gives Vanguard Charge rank 3",
                  (VANGUARD_CHARGE[1], VANGUARD_CHARGE[2]) in c.superseded[start:], "superseded %r" % c.superseded[start:])
    rows = charge_rows()
    results.check("Vanguard Charge rank 3 is the only active Charge",
                  rows == {CHARGE[2]: 0, VANGUARD_CHARGE[2]: 1}, "%r" % rows)
    button = charge_button()
    results.check("the button casts Vanguard Charge rank 3", button == VANGUARD_CHARGE[2],
                  "button %r" % button)

    c.logout()
    c.login_again()
    spells = c.initial_spells or set()
    results.check("a login keeps Vanguard Charge rank 3 and no stock Charge in the spellbook",
                  VANGUARD_CHARGE[2] in spells and not spells & set(CHARGE), "%r" % (spells & set(CHARGE + VANGUARD_CHARGE)))

    start = len(c.superseded)
    c.gm(".reset talents", select=me)
    c.pump(1)
    results.check("a talent reset gives back Charge rank 3 in the client",
                  (VANGUARD_CHARGE[2], CHARGE[2]) in c.superseded[start:], "superseded %r" % c.superseded[start:])
    rows = charge_rows()
    results.check("after the reset stock Charge rank 3 is active and no Vanguard Charge is known",
                  rows == {CHARGE[2]: 1}, "%r" % rows)
    button = charge_button()
    results.check("after the reset the button casts Charge rank 3", button == CHARGE[2],
                  "button %r" % button)

    learned, superseded = learn_vanguard()
    results.check("Vanguard again gives Vanguard Charge rank 3", (CHARGE[2], VANGUARD_CHARGE[2]) in superseded,
                  "superseded %r" % superseded)
    button = charge_button()
    results.check("before the switch the button casts Vanguard Charge rank 3", button == VANGUARD_CHARGE[2],
                  "button %r" % button)
    c.logout()
else:
    results.check("the first login under the vanilla rules switches", realm.wait_log(SWITCH % NAME, 5, c))
    spells = c.initial_spells or set()
    results.check("the login sends stock Charge rank 3 and no Vanguard Charge",
                  c.initial_spells is not None and CHARGE[2] in spells and not spells & set(VANGUARD_CHARGE),
                  "%r" % (spells & set(CHARGE + VANGUARD_CHARGE)))
    rows = charge_rows()
    results.check("stock Charge rank 3 is active, no Vanguard Charge is known",
                  rows.get(CHARGE[2]) == 1 and not set(rows) & set(VANGUARD_CHARGE), "%r" % rows)
    button = charge_button()
    results.check("the button casts stock Charge rank 3 again", button == CHARGE[2],
                  "button %r" % button)
    c.logout()
    c.login_again()
    results.check("a second login keeps stock Charge rank 3", CHARGE[2] in (c.initial_spells or set()))

results.finish()
