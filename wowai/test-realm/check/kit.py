"""The Python side of the in-game check kit.

A check script runs with `wowai/test-realm/realm.sh --ticket N check <script>`.
The kit marks the end of Server.log first and puts this folder on the
Python path. The script then writes only its scenario:

    from kit import Realm, Results
    realm = Realm()                      # the realm of the check command
    results = Results()
    client = realm.login("PLAYER62", "Thrallson")
    client.chat(CHAT_MSG_WHISPER, "invite", target="Grukk")
    client.pump(5)                       # prints the bot chat as it comes
    realm.mark("pull")
    client.chat(CHAT_MSG_PARTY, "pull")
    results.check("the tank pulls", realm.wait_log(r"Grukk: pull", 10, client))
    results.finish()

realm.roster() and realm.bot(role, faction) give the bots by role, so a
check names no bot (the roster changes).
pos(client, guid) and stat(client, guid) read a unit with GM commands.
dist(a, b), kill(client, guid), and revive_all(realm, client, bots) are the
small helpers that the checks share. spawn(realm, client), sturdy(client,
guid) and saved_spells(realm, client) serve the spell and talent checks,
with wowclient.SpellClient as the client class.
Realm(62) works outside the check command too.
"""
import collections
import math
import os
import re
import subprocess
import sys
import time

import wowclient

HERE = os.path.dirname(os.path.abspath(__file__))
KIT = os.path.join(HERE, "..", "realm.sh")


# The bot roles in the order of the bot panel, and the race ids of the Alliance.
ROLES = ["tank", "healer", "melee", "ranged"]
ALLIANCE_RACES = (1, 3, 4, 7)
# A bot of the roster (Realm.roster). faction: "horde" or "alliance".
Bot = collections.namedtuple("Bot", "name role race cls faction")
# A target for the combat checks: Mottled Boar (Durotar), a neutral beast that can be attacked.
BOAR = 3098
# A GM stun without an end (`.aura FREEZE`): the target stays where it is and does not hit back.
FREEZE = 9454


class Realm:
    """One realm of the kit. ticket: a number, "shared", or None for the realm
    of the check command (WOWAI_REALM_TICKET, else the realm that the
    WOWAI_REALM_* settings of `wowai/dev realm` name)."""

    def __init__(self, ticket=None):
        ticket = str(ticket or os.environ.get("WOWAI_REALM_TICKET") or "")
        if ticket == "shared":
            self.cli = [KIT, "--shared"]
        elif ticket:
            self.cli = [KIT, "--ticket", ticket]
        else:
            self.cli = [os.environ.get("WOWAI_REALM_CLI", os.path.join(HERE, "..", "..", "dev")), "realm"]
        self.settings = dict(line.split("=", 1) for line in self.run("env").splitlines() if "=" in line)
        self.host = "127.0.0.1"
        self.world_port = int(self.settings["WOWAI_REALM_WORLD_PORT"])
        self.dir = self.settings["WOWAI_REALM_DIR"]

    def run(self, *args):
        """A kit command. Its output, or RuntimeError when it fails."""
        done = subprocess.run(self.cli + list(args), capture_output=True, text=True)
        if done.returncode != 0:
            raise RuntimeError("realm %s failed: %s" % (" ".join(args), (done.stderr or done.stdout).strip()))
        return done.stdout.replace("\r", "")

    def soap(self, command):
        """A GM command over SOAP. The answer of mangosd."""
        return self.run("soap", command).strip()

    def sql(self, db, query):
        """The rows of a query, one line each, tab between the columns."""
        return self.run("sql", db, query).strip()

    def guid_of(self, name):
        """The guid of a character, or 0."""
        row = self.sql("characters", "SELECT guid FROM characters WHERE name = '%s'" % name)
        return int(row) if row else 0

    def bot_names(self):
        """guid -> name of each bot."""
        rows = self.sql("characters", "SELECT c.guid, c.name FROM characters c JOIN bot_character b ON b.guid = c.guid")
        return {int(guid): name for guid, name in (row.split("\t") for row in rows.splitlines() if row)}

    def roster(self, faction=None, role=None):
        """The group bots of the realm (no world bot), as Bot tuples, in the
        order of the bot panel (bots/BotPanel.h): by role (tank, healer,
        melee, ranged), then by name. faction: "horde" or "alliance"; role:
        a role word. A check names its bots by role through this, never by
        name: the roster changes."""
        rows = self.sql("characters", "SELECT c.name, b.role, c.race, c.class FROM characters c "
                        "JOIN bot_character b ON b.guid = c.guid WHERE b.world_bot = 0")
        bots = []
        for row in rows.splitlines():
            if not row:
                continue
            name, bot_role, race, cls = row.split("\t")
            bot = Bot(name, bot_role, int(race), int(cls),
                      "alliance" if int(race) in ALLIANCE_RACES else "horde")
            if (faction is None or bot.faction == faction) and (role is None or bot.role == role):
                bots.append(bot)
        return sorted(bots, key=lambda bot: (ROLES.index(bot.role), bot.name))

    def bot(self, role, faction="horde", n=0):
        """The name of the n-th bot of the role and faction in roster order."""
        return self.roster(faction, role)[n].name

    def mark(self, label="mark"):
        """Mark the end of Server.log: log() starts there."""
        self.run("mark", label)

    def log(self, all_lines=False):
        """The Server.log lines since the last mark, without the known noise."""
        return self.run("log", "--all") if all_lines else self.run("log")

    def wait_log(self, pattern, seconds, client=None):
        """Wait until a line since the last mark matches. Pumps the client
        meanwhile, so that it acks and echoes."""
        end = time.time() + seconds
        while True:
            if re.search(pattern, self.log()):
                return True
            if time.time() >= end:
                return False
            if client:
                client.pump(1)
            else:
                time.sleep(1)

    def login(self, account, character, race=2, cls=1, client_class=wowclient.WorldClient):
        """Log a player in (the account gets GM level 6; the character is made
        if the account has none). The client echoes the chat of the bots with
        their names."""
        key = self.run("key", account).strip()
        client = client_class(self.host, self.world_port, account, wowclient.key_bytes(key), names=self.bot_names())
        client.login(character, race=race, cls=cls)
        return client


def pos(client, guid):
    """(x, y, z) of a unit from `.gps`, or None."""
    for line in client.gm(".gps", select=guid):
        m = re.search(r"X: (-?[\d.]+) Y: (-?[\d.]+) Z: (-?[\d.]+)", line)
        if m:
            return tuple(float(v) for v in m.groups())
    return None


def stat(client, guid):
    """(health, max health, mana, max mana) of a unit from `.unit statinfo`;
    -1 for a value that the answer does not have."""
    values = {}
    for line in client.gm(".unit statinfo", select=guid, wait=1.0):
        m = re.match(r"(Health|Max Health|Mana|Max Mana): (\d+)", line)
        if m:
            values[m.group(1)] = int(m.group(2))
    return values.get("Health", -1), values.get("Max Health", -1), values.get("Mana", -1), values.get("Max Mana", -1)


def dist(a, b):
    """The ground distance between two (x, y, ...) places, without z."""
    return math.hypot(a[0] - b[0], a[1] - b[1])


def kill(client, guid):
    """Kills the unit with a GM `.die`; the GM answer lines."""
    return client.gm(".die", select=guid, wait=1.0)


def revive_all(realm, client, bots):
    """Revives the bots (names) over SOAP, and the GM character of `client`."""
    for bot in bots:
        realm.soap(".revive %s" % bot)
    client.gm(".revive", select=client.guid)


def spawn(realm, client, entry=BOAR, wait=1.5):
    """Spawns the creature `entry` at the client's place (GM `.npc spawn add`); its guid."""
    client.gm(".npc spawn add %d" % entry, wait=wait)
    low = int(realm.sql("mangos", "SELECT MAX(guid) FROM creature WHERE id = %d" % entry).split()[-1])
    return (0xF130 << 48) | (entry << 24) | low


def sturdy(client, guid, freeze=True):
    """Gives a spawned creature a million health and, with `freeze`, the endless GM stun."""
    client.gm(".modify hp 1000000 1000000", select=guid)
    if freeze:
        client.gm(".aura %d" % FREEZE, select=guid)


def saved_spells(realm, client, enabled_only=True):
    """The spells in character_spell of the client's character after a `.save`. With `enabled_only`,
    without the disabled rows (a spell that a switch or a lost talent took away)."""
    client.gm(".save", select=client.guid)
    client.pump(1)
    query = "SELECT spell FROM character_spell WHERE guid = %d" % client.guid
    if enabled_only:
        query += " AND disabled = 0"
    return {int(row) for row in realm.sql("characters", query).splitlines() if row}


class Results:
    """PASS and FAIL lines, and the exit code of a check."""

    def __init__(self):
        self.failures = 0

    def check(self, name, ok, detail=""):
        print(("PASS: " if ok else "FAIL: ") + name + (" (%s)" % detail if detail else ""))
        sys.stdout.flush()
        if not ok:
            self.failures += 1
        return ok

    def finish(self):
        print("FAILURES: %d" % self.failures)
        sys.exit(1 if self.failures else 0)
