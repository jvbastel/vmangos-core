"""Test data: a small navmesh tile written in the mmap v6 / Detour v7 format,
and small route files.

The tile is built from WoW positions. The writer stores each vertex as
Recast (y, z, x), the way the mmap generator does, so a reader that gets the
axes wrong reads other positions back.
"""

import struct
from pathlib import Path

MMAP_MAGIC = 0x4D4D4150
DT_MAGIC = ord("D") << 24 | ord("N") << 16 | ord("A") << 8 | ord("V")
GROUND = 0x01
MAGMA = 0x02
STEEP = 0x10

# The fixture floors, in WoW positions (x, y, z). The lower floor and the
# lava pool sit side by side at z -20. The upper floor (a bridge) sits over
# the middle of the lower floor at z -12.
LOWER = [(100, -50, -20), (110, -50, -20), (110, -40, -20), (100, -40, -20)]
UPPER = [(103, -47, -12), (107, -47, -12), (107, -43, -12), (103, -43, -12)]
LAVA = [(110, -50, -20), (114, -50, -20), (114, -40, -20), (110, -40, -20)]
# A second lava pool north of the lower floor, behind a steep bank: no walk
# joins it to the floor, but the pool is part of the dungeon.
BANK = [(100, -40, -20), (110, -40, -20), (110, -38, -19), (100, -38, -19)]
POOL = [(100, -38, -19), (110, -38, -19), (110, -34, -19), (100, -34, -19)]


def floor(points, flags=GROUND, area=1):
    return {"points": points, "flags": flags, "area": area}


# A flat plane far from the dungeon, not joined to it: the mmap generator
# leaves such planes in the tiles of some dungeons (the Deadmines).
OUTSIDE = [(300, 0, 50), (330, 0, 50), (330, 30, 50), (300, 30, 50)]

# Two tiles. The lava pool sits in the second tile, joined to the lower floor
# over the tile border at x 110.
FLOORS = [floor(LOWER), floor(UPPER), floor(BANK, flags=GROUND | STEEP, area=3), floor(POOL, flags=MAGMA, area=8)]
NEXT_TILE = [floor(LAVA, flags=MAGMA, area=8), floor(OUTSIDE)]


def _neighbours(polys, poly, border_x):
    """The Detour neis of a poly: 1 + the index of the poly of this tile
    with the same edge, DT_EXT_LINK on the tile border, else 0 (a wall)."""
    points = poly["points"]
    out = []
    for j, a in enumerate(points):
        b = points[(j + 1) % len(points)]
        other = next((k for k, q in enumerate(polys) for i, c in enumerate(q["points"])
                      if c == b and q["points"][(i + 1) % len(q["points"])] == a), None)
        if other is not None:
            out.append(other + 1)
        elif border_x is not None and a[0] == b[0] == border_x:
            out.append(0x8000)
        else:
            out.append(0)
    return out + [0] * (6 - len(out))


def tile_bytes(polys, mmap_version=6, dt_version=7, link_size=16, links=2, border_x=110):
    """One .mmtile: the 20-byte mmap header and the Detour tile data. Each
    poly is convex and has its own vertices; its detail mesh is a fan of
    triangles over the poly vertices. Poly edges on the line x = border_x
    are portals to the next tile."""
    verts = []
    poly_data = b""
    detail_meshes = b""
    detail_tris = b""
    tri_count = 0
    for poly in polys:
        first = len(verts)
        count = len(poly["points"])
        verts += [(y, z, x) for x, y, z in poly["points"]]
        indices = list(range(first, first + count)) + [0] * (6 - count)
        poly_data += struct.pack("<I6H6HHBB", 0xFFFFFFFF, *indices, *_neighbours(polys, poly, border_x), poly["flags"], count, poly["area"])
        tris = [(0, k, k + 1) for k in range(1, count - 1)]
        detail_meshes += struct.pack("<IIBBxx", 0, tri_count, 0, len(tris))
        detail_tris += b"".join(struct.pack("<4B", a, b, c, 0) for a, b, c in tris)
        tri_count += len(tris)
    xs = [v[0] for v in verts]
    ys = [v[1] for v in verts]
    zs = [v[2] for v in verts]
    header = struct.pack("<15i", DT_MAGIC, dt_version, 0, 0, 0, 0, len(polys), len(verts), links, len(polys),
                         0, tri_count, 0, 0, len(polys))
    header += struct.pack("<10f", 2.0, 0.5, 1.0, min(xs), min(ys), min(zs), max(xs), max(ys), max(zs), 1.0)
    body = (header + b"".join(struct.pack("<3f", *v) for v in verts) + poly_data + b"\0" * (link_size * links)
            + detail_meshes + detail_tris)
    return struct.pack("<5I", MMAP_MAGIC, dt_version, mmap_version, len(body), 0) + body


def write_tile(folder: Path, map_id: int, polys=FLOORS, next_tile=NEXT_TILE, **version):
    """The fixture tiles of the map: polys in tile 31,31 and next_tile in
    tile 31,32."""
    folder.mkdir(parents=True, exist_ok=True)
    (folder / f"{map_id:03d}3131.mmtile").write_bytes(tile_bytes(polys, **version))
    if next_tile:
        (folder / f"{map_id:03d}3132.mmtile").write_bytes(tile_bytes(next_tile, **version))


ROUTE = """\
# A small route over the fixture floors.
route fixture_cave 389
point p1 101.00 -49.00 -20.00 2.00
alt 101.50 -49.00 -20.00 2.00
point pull_pack1 104.00 -48.00 -20.00 2.00
pack pack1 108.00 -45.00 -20.00
mob 501 11318 # Ragefire Trogg
mob 502 11319 # Ragefire Shaman
point p2 105.00 -45.00 -12.00 2.00 quest 5723
point pull_bazzalan 102.00 -41.00 -20.00 2.00
pack bazzalan 101.00 -42.00 -20.00
mob 600 11519 # Bazzalan
"""


def write_route(folder: Path, name="fixture_cave", text=ROUTE) -> Path:
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / f"{name}.txt"
    path.write_text(text)
    return path
