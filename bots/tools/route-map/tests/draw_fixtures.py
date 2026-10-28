"""Test data of the route drawing: a cave of 10-yard cells, written as two
navmesh tiles with fixtures.write_tile.

    y 20..30   L7  L6  L5
    y 10..20   --wall--  L4          (no floor at x 0..20: a wall block)
    y  0..10   J  I  L1  L2  L3  R  UA     (I is lava, R a ramp up to z 8)
    y -10..0           UC1 UC2 ...  UB     (the upper walk at z 8)
         x: -20 -10  0  10  20  30  40  50

The lower floor is at z 0. The upper floor (z 8) runs from the top of the
ramp R round to UD, a ledge over L1: at (5, 5) the floors stack, z 0 and
z 8. The lava cell I and the island J sit in the second tile, over the tile
border at x 0.
"""

from fixtures import GROUND, MAGMA, floor


def cell(x0, y0, z=0.0, size=10, z_far=None):
    """A square cell from (x0, y0); z_far is the height of its x0 + size side (a ramp)."""
    far = z if z_far is None else z_far
    x1, y1 = x0 + size, y0 + size
    return [(x0, y0, z), (x1, y0, far), (x1, y1, far), (x0, y1, z)]


LOWER = [cell(0, 0), cell(10, 0), cell(20, 0), cell(20, 10), cell(20, 20), cell(10, 20), cell(0, 20)]
RAMP = cell(30, 0, 0.0, z_far=8.0)
UPPER = [cell(40, 0, 8), cell(40, -10, 8), [(10, -10, 8), (40, -10, 8), (40, 0, 8), (10, 0, 8)],
         cell(0, -10, 8), cell(0, 0, 8)]
CAVE = [floor(c) for c in LOWER + [RAMP] + UPPER]
ISLAND = [floor(cell(-10, 0), flags=MAGMA, area=8), floor(cell(-20, 0), flags=GROUND)]
BORDER_X = 0
