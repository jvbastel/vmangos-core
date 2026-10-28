"""The walked path of a run trace (jvbastel/WOWAI#152): the positions that
a scripted run wrote for each group member, drawn on the dungeon map."""

from test_map_web import client

TRACE = """\
0.0 run 3 ragefire_chasm full starts
0.5 Grukk at 101.00 -49.00 -20.00
0.5 Hela at 100.50 -49.50 -20.00
1.6 Grukk at 104.00 -48.00 -20.00
1.6 Grukk targets Ragefire Trogg 38043
2.0 Ezzra says in party: pull at 5 4 3
2.4 Ragefire Trogg 38043 flees at 108.00 -45.00 -20.00
3.0 Grukk casts 2457 Battle Stance
3.1 Hela at 103.00 -48.00 -20.00
4.2 Grukk at 105.00 -45.00 -12.00
610.3 Run 3 ragefire_chasm full: passed after 610 s
"""


def write_trace(tmp_path, text=TRACE):
    path = tmp_path / "runs" / "20261028-101500-ragefire_chasm-full.trace.txt"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    return path


def test_a_run_trace_loads_as_the_walked_path_of_each_member(tmp_path):
    web = client(tmp_path)
    trace = write_trace(tmp_path)

    walked = web.get("/api/maps/fixture_cave/walked", params={"trace": str(trace)}).json()

    assert walked["trace"] == trace.name
    assert walked["members"] == [
        {"name": "Grukk", "points": [{"t": 0.5, "x": 101.0, "y": -49.0, "z": -20.0},
                                     {"t": 1.6, "x": 104.0, "y": -48.0, "z": -20.0},
                                     {"t": 4.2, "x": 105.0, "y": -45.0, "z": -12.0}]},
        {"name": "Hela", "points": [{"t": 0.5, "x": 100.5, "y": -49.5, "z": -20.0},
                                    {"t": 3.1, "x": 103.0, "y": -48.0, "z": -20.0}]},
    ]


def test_a_missing_trace_is_a_clear_error(tmp_path):
    response = client(tmp_path).get("/api/maps/fixture_cave/walked", params={"trace": str(tmp_path / "none.trace.txt")})

    assert response.status_code == 404
    assert "none.trace.txt" in response.json()["detail"]


def test_a_trace_with_no_member_positions_is_a_clear_error(tmp_path):
    web = client(tmp_path)
    trace = write_trace(tmp_path, "0.0 run 3 ragefire_chasm full starts\n")

    response = web.get("/api/maps/fixture_cave/walked", params={"trace": str(trace)})

    assert response.status_code == 422
    assert "no member positions" in response.json()["detail"]


def test_the_page_has_the_walked_path_layer(tmp_path):
    web = client(tmp_path)

    page = web.get("/").text
    script = web.get("/walked_path.js")

    assert 'src="/walked_path.js"' in page
    assert script.status_code == 200
    assert "/walked?trace=" in script.text
