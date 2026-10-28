"""The point and line distances of the map (geometry.py)."""

import math

from geometry import closest_on_segment, polyline_distance, segment_distance


def test_the_nearest_point_of_a_line_is_its_end_when_the_point_is_past_it():
    assert closest_on_segment((15, 3), (0, 0), (10, 0)) == (10, 0)
    assert closest_on_segment((4, 3), (0, 0), (10, 0)) == (4, 0)


def test_the_distance_to_a_line_in_three_dimensions():
    assert segment_distance((5, 3, 4), (0, 0, 0), (10, 0, 0)) == 5.0


def test_the_distance_to_a_polyline_is_to_its_nearest_piece():
    assert polyline_distance((10, 5, 0), [(0, 0, 0), (10, 0, 0), (10, 20, 0)]) == 0.0
    assert polyline_distance((3, 4, 0), [(0, 0, 0)]) == 5.0
    assert polyline_distance((3, 4, 0), []) == math.inf
