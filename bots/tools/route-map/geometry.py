"""Point and line distances of the dungeon map, in yards. A point is a
tuple of coordinates: (x, y) or (x, y, z). The route library's own copies
are in src/bots (RouteGenerator.cpp, RouteHandOrder.cpp)."""

from __future__ import annotations

import math
from typing import Sequence

Point = Sequence[float]


def closest_on_segment(p: Point, a: Point, b: Point) -> tuple[float, ...]:
    """The point of the straight line from a to b that is nearest to p."""
    ab = [bi - ai for ai, bi in zip(a, b)]
    length2 = sum(c * c for c in ab)
    t = 0.0 if length2 == 0 else max(0.0, min(1.0, sum((pi - ai) * c for pi, ai, c in zip(p, a, ab)) / length2))
    return tuple(ai + t * c for ai, c in zip(a, ab))


def segment_distance(p: Point, a: Point, b: Point) -> float:
    """The distance from p to the straight line from a to b."""
    return math.dist(p, closest_on_segment(p, a, b))


def polyline_distance(p: Point, points: Sequence[Point]) -> float:
    """The distance from p to the line through the points, in their order
    (to the one point when there is one; infinite without points)."""
    pieces = list(zip(points, points[1:])) or [(q, q) for q in points]
    return min((segment_distance(p, a, b) for a, b in pieces), default=math.inf)
