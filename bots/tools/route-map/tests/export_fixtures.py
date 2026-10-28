"""Test data: a small export file (`.route export`, src/bots/RouteExport.h)
for the fixture route of fixtures.py. Map 389.

- pack 501 (Trogg, Shaman) and pack 600 (Bazzalan, a boss) are on the
  fixture route, as pack1 and bazzalan;
- pack 700 is not on the route: a Trogg that wanders 5 yards and a Shaman
  that patrols three waypoints;
- pack 800 is not on the route: Jergosh, a boss.
"""

from pathlib import Path

import export_file

EXPORT = """\
# The export of map 389 (.route export, WOWAI#150).
export 389
boss 11519
boss 11518
pack 501
mob 501 11318 108.00 -45.00 -20.00 0.00
mob 502 11319 109.00 -46.00 -20.00 2.00
pack 600
mob 600 11519 101.00 -42.00 -20.00 0.00
pack 700
mob 700 11318 102.00 -48.00 -20.00 5.00
mob 701 11319 103.00 -48.50 -20.00 0.00
wp 103.00 -48.50 -20.00
wp 106.00 -48.00 -20.00
wp 106.00 -44.00 -20.00
pack 800
mob 800 11518 109.00 -41.00 -20.00 0.00
quest 5723
kill 11318 8
creature 11318 Ragefire Trogg
creature 11319 Ragefire Shaman
creature 11519 Bazzalan
creature 11518 Jergosh the Invoker
questname 5723 Testing an Enemy's Strength
"""


def write_export(folder: Path, map_id=389, text=EXPORT) -> Path:
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / export_file.file_name(map_id)
    path.write_text(text)
    return path
