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

Realm(62) works outside the check command too.
"""
import os
import re
import subprocess
import sys
import time

import wowclient

HERE = os.path.dirname(os.path.abspath(__file__))
KIT = os.path.join(HERE, "..", "realm.sh")


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
