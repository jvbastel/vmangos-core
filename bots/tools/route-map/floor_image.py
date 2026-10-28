"""The floor image of the dungeon map: the navmesh floor drawn top-down as a
flat image, north (+x) up and west (+y) left.

The floor has one colour. Walls and floor breaks (poly edges with no
neighbour) are gaps in the background colour. Lava has its own colour. The
polys are drawn from low to high, so a higher floor covers a lower one, and
a lower floor is dimmer. There are no triangle edges and no height colours.
"""

from __future__ import annotations

import io
import math
from dataclasses import dataclass
from typing import Optional

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.collections import PolyCollection  # noqa: E402

BACKGROUND = (0.11, 0.12, 0.13)
FLOOR = (0.86, 0.82, 0.74)
LAVA = (0.80, 0.33, 0.15)
DIMMEST = 0.4  # the shade of the lowest floor; the highest floor has shade 1
WALL_WIDTH = 0.5  # yards
PIXELS_PER_YARD = 6
MAX_PIXELS = 4000


class EmptyFloor(Exception):
    pass


@dataclass
class FloorImage:
    png: bytes
    bounds: dict  # x_min x_max y_min y_max z_min z_max of the drawn floor, WoW yards


def _plot(point):
    """A WoW (x, y) as a plot position: x right = -y (east), y up = x (north)."""
    return (-point[1], point[0])


def _shade(base, shade):
    return tuple(b * shade + g * (1 - shade) for b, g in zip(base, BACKGROUND))


def _wall(a, b):
    (ax, ay), (bx, by) = _plot(a), _plot(b)
    length = math.hypot(bx - ax, by - ay) or 1.0
    nx, ny = -(by - ay) / length * WALL_WIDTH / 2, (bx - ax) / length * WALL_WIDTH / 2
    return [(ax + nx, ay + ny), (bx + nx, by + ny), (bx - nx, by - ny), (ax - nx, ay - ny)]


def render(polys, z_min: Optional[float] = None, z_max: Optional[float] = None) -> FloorImage:
    """The image of the polys whose mean height is in [z_min, z_max]."""
    low = -math.inf if z_min is None else z_min
    high = math.inf if z_max is None else z_max
    drawn = sorted((p for p in polys if low <= p.mean_z <= high), key=lambda p: p.mean_z)
    if not drawn:
        raise EmptyFloor(f"no floor between heights {z_min} and {z_max}")
    points = [q for p in drawn for q in p.points]
    bounds = {"x_min": min(q[0] for q in points), "x_max": max(q[0] for q in points),
              "y_min": min(q[1] for q in points), "y_max": max(q[1] for q in points),
              "z_min": min(q[2] for q in points), "z_max": max(q[2] for q in points)}
    bounds = {k: round(v, 2) for k, v in bounds.items()}

    z_low, z_high = drawn[0].mean_z, drawn[-1].mean_z
    shapes, colours = [], []
    for poly in drawn:
        rise = (poly.mean_z - z_low) / (z_high - z_low) if z_high > z_low else 1.0
        colour = _shade(LAVA if poly.lava else FLOOR, DIMMEST + (1 - DIMMEST) * rise)
        for tri in poly.tris:
            shapes.append([_plot(q) for q in tri])
            colours.append(colour)
        for a, b in poly.walls:
            shapes.append(_wall(a, b))
            colours.append(BACKGROUND)

    width = bounds["y_max"] - bounds["y_min"] or 1.0
    height = bounds["x_max"] - bounds["x_min"] or 1.0
    scale = min(PIXELS_PER_YARD, MAX_PIXELS / max(width, height))
    dpi = 100
    fig = plt.figure(figsize=(width * scale / dpi, height * scale / dpi), dpi=dpi)
    ax = fig.add_axes((0, 0, 1, 1))
    ax.set_axis_off()
    ax.set_facecolor(BACKGROUND)
    # The stroke in the face colour hides the seams between triangles.
    ax.add_collection(PolyCollection(shapes, facecolors=colours, edgecolors=colours, linewidths=1.0))
    ax.set_xlim(-bounds["y_max"], -bounds["y_min"])
    ax.set_ylim(bounds["x_min"], bounds["x_max"])
    out = io.BytesIO()
    fig.savefig(out, format="png", dpi=dpi, facecolor=BACKGROUND)
    plt.close(fig)
    return FloorImage(out.getvalue(), bounds)
