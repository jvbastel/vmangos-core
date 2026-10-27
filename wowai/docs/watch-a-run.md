# Follow a run on a phone

The watch script `wowai/test-realm/watch.py` serves one small web page about the scripted runs of a test game server. The page reloads every 5 seconds. It shows:

- The run (run N of M), the scenario, the run time, the phase, the current script line, and the last pack.
- Each member: alive or dead, its target, its last cast, and a tick for each of the last fights (✓ took part, ✗ did not join).
- The last events: pulls, fight summaries, deaths, wipes, boss kills, threat losses, chases, and members that did not join.
- The end line of each run, with the `threshold_failures` of a full run.

The script reads only files that mangosd writes: the `[Bots] Run` lines of `Server.log`, and the live trace `live.trace.txt` in the runs folder. The run starts that file empty. It only needs SOAP for the phase line (`--status`).

## Start it

Start the runs and the page with one command:

```
wowai/test-realm/runs.py --ticket <N> --watch full 3
```

`--watch` takes a port (default 8098). The script prints the address to open, for example `Watch the runs at http://192.168.1.20:8098/`. The page stays up for 10 minutes after the last run. Ctrl+C stops it at once.

To watch runs that something else starts, run the watch script alone:

```
wowai/test-realm/watch.py --ticket <N> --status
```

## Open the port once (Windows)

WSL uses `networkingMode=mirrored`, so the port of the script is the port of the PC on the home Wi-Fi. Windows blocks it until you add a firewall rule. Do this once, in a PowerShell window that you started as administrator:

```
New-NetFirewallRule -DisplayName "WOWAI watch page" -Direction Inbound -Protocol TCP -LocalPort 8098 -Action Allow -Profile Private
```

The rule is for private networks only. If Windows marks the home Wi-Fi as public, set it to private in the Wi-Fi settings. Then, on the phone, on the same Wi-Fi, open the address that the script printed.

The Hyper-V firewall of WSL can also block the port. If the page does not open, run this once too:

```
Set-NetFirewallHyperVVMSetting -Name '{40E0AC32-46A5-438A-A0B2-2B479E8F2E90}' -DefaultInboundAction Allow
```

## Pushes (optional)

With `--ntfy <topic>`, the script also sends pushes through ntfy.sh: the run start, each wipe, each boss kill, each member that did not join a fight, and the run end. It sends at most one push of each kind per 30 seconds. A push goes out from the PC, so no port needs to be open for it.

1. Install the ntfy app on the phone.
2. Choose a random topic name, for example `wowai-7f3k9q2m`. Anyone who knows the name can read the topic, so the script sends only game events.
3. In the app, subscribe to the topic.
4. Start the runs: `wowai/test-realm/runs.py --ticket <N> --watch --ntfy wowai-7f3k9q2m full 3`.

## The fight summary

When a fight ends (the group leaves combat after the pull), mangosd logs one line for it, and the result file gets a `fights[]` entry:

```
[Bots] Run 3: fight 7 pack7: Ezzra 1.2 s 3 casts, Grukk 0.4 s 0 casts, Hela 2.0 s 1 cast, Morzul did not join
```

The number is the pull of the run. To take part, a member attacks a mob of the fight, casts at one, or casts a heal at a member. A target alone does not count. The seconds run from the pull to its first part. The cast count holds only casts with a cast time or a channel: an instant cast is never a current cast. The rule is `Bots::FightWatch` (`src/bots/FightWatch.h`).
