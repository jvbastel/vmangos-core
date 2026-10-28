"""Floor queries of the route drawing: snap a click to the floor, and join
two points with a path along the floor (ticket jvbastel/WOWAI#157).

The path is an A* search over the navmesh polygon links, then a string pull
through the portals of the polygons it crosses, then points a few yards
apart at floor height. It walks only the polygons that the server's player
path filter lets a player walk (PathFinder.cpp createFilter: NAV_GROUND or
NAV_WATER; magma and slime are left out). It is not Detour, so the path can
differ a little from the server's path. When no lava-free path joins the
points, it takes a path over lava, with a warning.
"""

from __future__ import annotations

import heapq
import math
from dataclasses import dataclass, field

import navmesh

PLAYER_FLAGS = 0x01 | 0x08  # NAV_GROUND | NAV_WATER: the include flags of a player's path filter
STEP = 3.0  # yards between two points of a path
LEVEL_CHANGE = 3.0  # a height change of this many yards goes to another level
LONG_WAY = 2.0  # a path this many times the straight distance is a long way round
LONG_WAY_MIN = 10.0  # ... and at least this many yards longer


@dataclass
class Piece:
    """The floor path from one point to the next."""
    points: list[navmesh.Vec]
    length: float
    straight: float
    warnings: list[dict] = field(default_factory=list)


def _cross(o, a, b):
    return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])


def _dist(a, b):
    return math.dist(a, b)


def _height(poly, x, y):
    for tri in poly.tris:
        z = navmesh._height_on(tri, x, y)
        if z is not None:
            return z
    return None


def _closest_on_segment(p, a, b):
    ax, ay, bx, by = a[0], a[1], b[0], b[1]
    dx, dy = bx - ax, by - ay
    length2 = dx * dx + dy * dy
    t = 0.0 if length2 == 0 else max(0.0, min(1.0, ((p[0] - ax) * dx + (p[1] - ay) * dy) / length2))
    return (ax + t * dx, ay + t * dy)


def _centre(poly):
    n = len(poly.points)
    return (sum(q[0] for q in poly.points) / n, sum(q[1] for q in poly.points) / n)


class FloorGraph:
    """The walkable polygons of a map and the portals that join them."""

    def __init__(self, mesh: navmesh.Navmesh):
        self.polys = mesh.polys
        index = {id(p): i for i, p in enumerate(self.polys)}
        self.links: list[list[tuple[int, tuple]]] = [[] for _ in self.polys]
        for i, poly in enumerate(self.polys):
            for other in poly.joined:
                j = index.get(id(other))
                if j is not None:
                    edge = self._shared_edge(poly, other)
                    if edge:
                        self.links[i].append((j, edge))
        lines = {}
        for i, poly in enumerate(self.polys):
            for edge in poly.portals:
                line, span, z = navmesh._portal_line(edge)
                lines.setdefault(line, []).append((span, z, i))
        for (axis, at), portals in lines.items():
            for k, (span, z, i) in enumerate(portals):
                for other_span, other_z, j in portals[k + 1:]:
                    low, high = max(span[0], other_span[0]), min(span[1], other_span[1])
                    if high - low > 0.01 and abs(z - other_z) < 3:
                        edge = ((at, low), (at, high)) if axis == "x" else ((low, at), (high, at))
                        self.links[i].append((j, edge))
                        self.links[j].append((i, edge))

    @staticmethod
    def _shared_edge(poly, other):
        others = {(round(q[0], 2), round(q[1], 2), round(q[2], 2)) for q in other.points}
        n = len(poly.points)
        for k in range(n):
            a, b = poly.points[k], poly.points[(k + 1) % n]
            if all((round(q[0], 2), round(q[1], 2), round(q[2], 2)) in others for q in (a, b)):
                return ((a[0], a[1]), (b[0], b[1]))
        return None

    def walkable(self, i, lava_too=False):
        flags = self.polys[i].flags
        return bool(flags & PLAYER_FLAGS) or (lava_too and bool(flags & navmesh.LAVA))

    def floors_at(self, x, y):
        """(z, poly index) of each floor at (x, y), lowest first."""
        found = [(z, i) for i, p in enumerate(self.polys) if (z := _height(p, x, y)) is not None]
        return sorted(found)

    def snap(self, x: float, y: float, z: float | None = None):
        """The floor point of a click and the floor heights there. Where
        floors stack, the floor nearest z (the top floor without z). A click
        off the floor (in a wall) goes to the nearest floor point."""
        found = self.floors_at(x, y)
        if not found:
            best = None
            for poly in self.polys:
                n = len(poly.points)
                for k in range(n):
                    q = _closest_on_segment((x, y), poly.points[k], poly.points[(k + 1) % n])
                    h = _height(poly, *q)
                    if h is None:
                        continue
                    score = (round(math.hypot(q[0] - x, q[1] - y), 3), abs(h - z) if z is not None else -h)
                    if best is None or score < best[0]:
                        best = (score, q)
            if best is None:
                return None, []
            x, y = best[1]
            found = self.floors_at(x, y)
        heights = [h for h, _ in found]
        chosen = min(heights, key=lambda h: abs(h - z)) if z is not None else heights[-1]
        return (x, y, chosen), heights

    def _poly_at(self, point):
        found = self.floors_at(point[0], point[1])
        return min(found, key=lambda f: abs(f[0] - point[2]))[1] if found else None

    def _search(self, start, goal, a, b, lava_too):
        """The polys from start to goal and the portals between them (A*)."""
        best = {start: 0.0}
        came = {}
        entry = {start: (a[0], a[1])}
        queue = [(_dist(a[:2], b[:2]), start)]
        while queue:
            _, i = heapq.heappop(queue)
            if i == goal:
                polys, portals = [goal], []
                while polys[-1] != start:
                    j, edge = came[polys[-1]]
                    polys.append(j)
                    portals.append(edge)
                return polys[::-1], portals[::-1]
            for j, edge in self.links[i]:
                if not self.walkable(j, lava_too) and j != goal:
                    continue
                mid = ((edge[0][0] + edge[1][0]) / 2, (edge[0][1] + edge[1][1]) / 2)
                cost = best[i] + _dist(entry[i], mid)
                if cost < best.get(j, math.inf):
                    best[j], came[j], entry[j] = cost, (i, edge), mid
                    heapq.heappush(queue, (cost + _dist(mid, b[:2]), j))
        return None

    def path(self, a: navmesh.Vec, b: navmesh.Vec) -> Piece:
        """The floor path from a to b, with its warnings."""
        straight = _dist(a, b)
        start, goal = self._poly_at(a), self._poly_at(b)
        found = None
        warnings = []
        if start is not None and goal is not None:
            found = self._search(start, goal, a, b, lava_too=False)
            if found is None or any(self.polys[i].lava for i in found[0]):
                found = found or self._search(start, goal, a, b, lava_too=True)
                if found:
                    warnings.append({"kind": "lava", "text": "the path goes over lava"})
        if found is None:
            return Piece([a, b], straight, straight,
                         [{"kind": "no_path", "text": "no floor path joins the points: a straight line"}])
        polys, portals = found
        corners = _string_pull(a, b, polys, portals, self.polys)
        points = self._dense(corners, [self.polys[i] for i in polys], a[2], b)
        length = sum(_dist(p, q) for p, q in zip(points, points[1:]))
        if (abs(b[2] - a[2]) >= LEVEL_CHANGE and length > LONG_WAY * straight
                and length - straight > LONG_WAY_MIN):
            warnings.append({"kind": "level", "text": f"another level by a long way round: "
                             f"{length:.0f} yards for {straight:.0f} yards straight"})
        return Piece(points, length, straight, warnings)

    def _dense(self, corners, corridor, z, end):
        """Points STEP yards apart or less along the corners, at the floor
        height of the corridor polys (the floor nearest the last height)."""
        points = [(corners[0][0], corners[0][1], z)]
        for p, q in zip(corners, corners[1:]):
            steps = max(1, math.ceil(_dist(p, q) / STEP))
            for k in range(1, steps + 1):
                x, y = p[0] + (q[0] - p[0]) * k / steps, p[1] + (q[1] - p[1]) * k / steps
                heights = [h for poly in corridor if (h := _height(poly, x, y)) is not None]
                if heights:
                    z = min(heights, key=lambda h: abs(h - z))
                points.append((x, y, z))
        points[-1] = end
        return points


def _string_pull(a, b, polys, portals, all_polys):
    """The corners of the shortest 2D line from a to b through the portals
    (the simple stupid funnel). A portal's left end is to the left of the
    way from the poly before it to the portal."""
    gates = [((a[0], a[1]), (a[0], a[1]))]
    for i, (p, q) in zip(polys, portals):
        c = _centre(all_polys[i])
        mid = ((p[0] + q[0]) / 2, (p[1] + q[1]) / 2)
        way = (c, mid)
        left, right = (p, q) if _cross(way[0], way[1], p) > _cross(way[0], way[1], q) else (q, p)
        gates.append((left, right))
    gates.append(((b[0], b[1]), (b[0], b[1])))
    corners = [(a[0], a[1])]
    apex = left = right = (a[0], a[1])
    apex_i = left_i = right_i = 0
    i = 1
    while i < len(gates):
        pl, pr = gates[i]
        if _cross(apex, right, pr) >= 0:  # the right side closes in
            if apex == right or _cross(apex, left, pr) < 0:
                right, right_i = pr, i
            else:  # past the left side: its end is a corner
                corners.append(left)
                apex, apex_i = left, left_i
                left = right = apex
                left_i = right_i = apex_i
                i = apex_i + 1
                continue
        if _cross(apex, left, pl) <= 0:  # the left side closes in
            if apex == left or _cross(apex, right, pl) > 0:
                left, left_i = pl, i
            else:
                corners.append(right)
                apex, apex_i = right, right_i
                left = right = apex
                left_i = right_i = apex_i
                i = apex_i + 1
                continue
        i += 1
    if corners[-1] != (b[0], b[1]):
        corners.append((b[0], b[1]))
    return corners
