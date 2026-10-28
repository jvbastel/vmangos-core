"""The navmesh reader: the polygons of the mmap tiles of one map, in WoW
positions.

The tiles are those of the vmangos mmap generator (contrib/mmap): mmap
version 6 (src/game/Maps/MoveMapSharedDefines.h), Detour data version 7
with 64-bit poly refs, so 16-byte links (dep/recastnavigation). A tile file
is <map:3><x:2><y:2>.mmtile: a 20-byte mmap header, then the Detour tile
data. Detour stores a vertex as Recast (x, y, z), which is WoW (y, z, x).
Any other version is an error: the reader never guesses a layout.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from pathlib import Path

MMAP_MAGIC = 0x4D4D4150  # 'MMAP'
MMAP_VERSION = 6
DT_NAVMESH_MAGIC = ord("D") << 24 | ord("N") << 16 | ord("A") << 8 | ord("V")
DT_NAVMESH_VERSION = 7
LINK_SIZE = 16  # dtLink with a 64-bit dtPolyRef
DT_EXT_LINK = 0x8000
DT_POLYTYPE_GROUND = 0

# NavTerrain in MoveMapSharedDefines.h: the poly flags.
NAV_MAGMA = 0x02
NAV_SLIME = 0x04
NAV_STEEP_SLOPES = 0x10
LAVA = NAV_MAGMA | NAV_SLIME  # the liquids a player path leaves out (PathFinder.cpp)
LAVA_MARGIN = 10.0


class NavmeshError(Exception):
    pass


Vec = tuple[float, float, float]


@dataclass
class Poly:
    points: list[Vec]  # the poly vertices, WoW (x, y, z)
    tris: list[tuple[Vec, Vec, Vec]]  # the detail triangles: the real floor heights
    flags: int
    walls: list[tuple[Vec, Vec]] = field(default_factory=list)  # edges with no neighbour
    portals: list[tuple[Vec, Vec]] = field(default_factory=list)  # edges on the tile border to the next tile
    joined: list["Poly"] = field(default_factory=list, repr=False)  # the neighbours in the same tile

    @property
    def lava(self) -> bool:
        return bool(self.flags & LAVA)

    @property
    def steep(self) -> bool:
        return bool(self.flags & NAV_STEEP_SLOPES)

    @property
    def mean_z(self) -> float:
        return sum(p[2] for p in self.points) / len(self.points)


def _align4(n: int) -> int:
    return (n + 3) // 4 * 4


def _wow(v) -> Vec:
    return (v[2], v[0], v[1])


def read_tile(path: Path) -> list[Poly]:
    data = path.read_bytes()
    if len(data) < 20 + 100:
        raise NavmeshError(f"{path.name}: too short for a navmesh tile ({len(data)} bytes)")
    magic, dt_version, mmap_version, size, _liquids = struct.unpack_from("<5I", data, 0)
    if magic != MMAP_MAGIC:
        raise NavmeshError(f"{path.name}: not an mmap tile (magic {magic:#x})")
    if mmap_version != MMAP_VERSION or dt_version != DT_NAVMESH_VERSION:
        raise NavmeshError(f"{path.name}: mmap version {mmap_version}, Detour version {dt_version}; the map reads "
                           f"only mmap version {MMAP_VERSION} with Detour version {DT_NAVMESH_VERSION}")
    (dmagic, dversion, _tx, _ty, _layer, _uid, poly_count, vert_count, link_count, detail_mesh_count,
     detail_vert_count, detail_tri_count, bv_count, offmesh_count, _offmesh_base) = struct.unpack_from("<15i", data, 20)
    if dmagic != DT_NAVMESH_MAGIC or dversion != DT_NAVMESH_VERSION:
        raise NavmeshError(f"{path.name}: Detour tile magic {dmagic:#x} version {dversion}; the map reads only "
                           f"Detour version {DT_NAVMESH_VERSION}")
    verts_at = 20 + 100
    polys_at = verts_at + _align4(12 * vert_count)
    links_at = polys_at + _align4(32 * poly_count)
    detail_meshes_at = links_at + _align4(LINK_SIZE * link_count)
    detail_verts_at = detail_meshes_at + _align4(12 * detail_mesh_count)
    detail_tris_at = detail_verts_at + _align4(12 * detail_vert_count)
    bv_at = detail_tris_at + _align4(4 * detail_tri_count)
    end = bv_at + _align4(16 * bv_count) + _align4(36 * offmesh_count)
    if end - 20 != size or len(data) < end:
        raise NavmeshError(f"{path.name}: the tile data is {size} bytes, but the layout of Detour version "
                           f"{DT_NAVMESH_VERSION} with 64-bit poly refs needs {end - 20}: another Detour build?")

    verts = [_wow(struct.unpack_from("<3f", data, verts_at + 12 * i)) for i in range(vert_count)]
    detail_verts = [_wow(struct.unpack_from("<3f", data, detail_verts_at + 12 * i))
                    for i in range(detail_vert_count)]
    polys = {}
    neighbours_of = {}
    for i in range(poly_count):
        at = polys_at + 32 * i
        indices = struct.unpack_from("<6H", data, at + 4)
        neighbours = struct.unpack_from("<6H", data, at + 16)
        flags, count, area_and_type = struct.unpack_from("<HBB", data, at + 28)
        if area_and_type >> 6 != DT_POLYTYPE_GROUND:
            continue  # an off-mesh connection, not floor
        points = [verts[v] for v in indices[:count]]
        vert_base, tri_base, _dverts, tri_count = struct.unpack_from("<IIBB", data, detail_meshes_at + 12 * i)

        def vertex(k):
            return points[k] if k < count else detail_verts[vert_base + k - count]

        tris = [tuple(vertex(k) for k in struct.unpack_from("<3B", data, detail_tris_at + 4 * (tri_base + t)))
                for t in range(tri_count)]
        edges = [(points[j], points[(j + 1) % count]) for j in range(count)]
        polys[i] = Poly(points, tris, flags, [e for e, n in zip(edges, neighbours) if n == 0],
                        [e for e, n in zip(edges, neighbours) if n & DT_EXT_LINK])
        neighbours_of[i] = [n - 1 for n in neighbours[:count] if n and not n & DT_EXT_LINK]
    for i, poly in polys.items():
        poly.joined = [polys[n] for n in neighbours_of[i] if n in polys]
    return list(polys.values())


def read_map(folder: Path, map_id: int) -> list[Poly]:
    """All polygons of the map's tiles in folder."""
    tiles = sorted(Path(folder).glob(f"{map_id:03d}[0-9][0-9][0-9][0-9].mmtile"))
    if not tiles:
        raise NavmeshError(f"no navmesh tiles of map {map_id} in {folder}")
    return [poly for tile in tiles for poly in read_tile(tile)]


def _height_on(tri, x, y):
    (x1, y1, z1), (x2, y2, z2), (x3, y3, z3) = tri
    d = (y2 - y3) * (x1 - x3) + (x3 - x2) * (y1 - y3)
    if abs(d) < 1e-9:
        return None
    a = ((y2 - y3) * (x - x3) + (x3 - x2) * (y - y3)) / d
    b = ((y3 - y1) * (x - x3) + (x1 - x3) * (y - y3)) / d
    c = 1 - a - b
    if min(a, b, c) < -1e-6:
        return None
    return a * z1 + b * z2 + c * z3


@dataclass
class Floor:
    z: float
    poly: Poly


def _portal_line(edge):
    """The tile border line of a portal edge and the edge's span on it."""
    (ax, ay, az), (bx, by, bz) = edge
    if abs(ax - bx) < 0.01:
        return ("x", round(ax, 1)), (min(ay, by), max(ay, by)), (az + bz) / 2
    return ("y", round(ay, 1)), (min(ax, bx), max(ax, bx)), (az + bz) / 2


def _parts(polys):
    """The polys grouped into floor parts: polys joined in their tile, and
    polys whose portals meet over a tile border. Steep polys join nothing:
    a chain of steep slopes would join the terrain that the mmap generator
    leaves round a dungeon."""
    parent = {id(p): id(p) for p in polys}

    def root(k):
        while parent[k] != k:
            parent[k] = parent[parent[k]]
            k = parent[k]
        return k

    def join(a, b):
        if not (a.steep or b.steep):
            parent[root(id(a))] = root(id(b))

    lines = {}
    for poly in polys:
        for other in poly.joined:
            if id(other) in parent:
                join(poly, other)
        for edge in poly.portals:
            line, span, z = _portal_line(edge)
            lines.setdefault(line, []).append((span, z, poly))
    for portals in lines.values():
        for k, (span, z, poly) in enumerate(portals):
            for other_span, other_z, other in portals[k + 1:]:
                if min(span[1], other_span[1]) - max(span[0], other_span[0]) > 0.01 and abs(z - other_z) < 3:
                    join(poly, other)
    parts = {}
    for poly in polys:
        parts.setdefault(root(id(poly)), []).append(poly)
    return parts, root


def _box(points):
    return (min(q[0] for q in points), max(q[0] for q in points), min(q[1] for q in points), max(q[1] for q in points))


def _overlap(a, b, margin=0.0):
    return a[0] - margin <= b[1] and b[0] <= a[1] + margin and a[2] - margin <= b[3] and b[2] <= a[3] + margin


class Navmesh:
    """The floor of a map: its walkable polygons (steep slopes left out)."""

    def __init__(self, polys: list[Poly]):
        self.polys = [p for p in polys if not p.steep]
        self._boxes = [(min(q[0] for q in p.points), max(q[0] for q in p.points),
                        min(q[1] for q in p.points), max(q[1] for q in p.points)) for p in self.polys]

    @classmethod
    def load(cls, folder: Path, map_id: int) -> "Navmesh":
        return cls(read_map(folder, map_id))

    def floors(self, x: float, y: float) -> list[Floor]:
        """The floors at (x, y), lowest first."""
        found = []
        for poly, (x0, x1, y0, y1) in zip(self.polys, self._boxes):
            if x0 - 0.01 <= x <= x1 + 0.01 and y0 - 0.01 <= y <= y1 + 0.01:
                for tri in poly.tris:
                    z = _height_on(tri, x, y)
                    if z is not None:
                        found.append(Floor(z, poly))
                        break
        return sorted(found, key=lambda f: f.z)

    def floor_near(self, x: float, y: float, z: float, within: float = 6.0):
        """The floor at (x, y) nearest height z, if one is within that many yards."""
        near = [f for f in self.floors(x, y) if abs(f.z - z) <= within]
        return min(near, key=lambda f: abs(f.z - z)) if near else None

    def joined_to(self, positions) -> list[Poly]:
        """The floor of the dungeon: the floor parts that hold one of the
        positions (x, y, z), and the lava parts in their area, give or take
        LAVA_MARGIN yards (a lava pool behind steep banks). Not the planes that the mmap generator leaves
        outside the dungeon. All polys when no position stands on a floor."""
        parts, root = _parts(self.polys)
        keep = set()
        for x, y, z in positions:
            found = self.floor_near(x, y, z)
            if found:
                keep.add(root(id(found.poly)))
        if not keep:
            return list(self.polys)
        area = _box([q for k in keep for p in parts[k] for q in p.points])
        for k, part in parts.items():
            if any(p.lava for p in part) and _overlap(area, _box([q for p in part for q in p.points]), LAVA_MARGIN):
                keep.add(k)
        return [p for k in keep for p in parts[k]]

    def bounds(self) -> dict:
        xs = [q[0] for p in self.polys for q in p.points]
        ys = [q[1] for p in self.polys for q in p.points]
        zs = [q[2] for p in self.polys for q in p.points]
        return {"x_min": min(xs), "x_max": max(xs), "y_min": min(ys), "y_max": max(ys),
                "z_min": min(zs), "z_max": max(zs)}
