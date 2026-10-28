"""The draft line of the route drawing through the web API: load the line
of the generated route or of an existing trace, and save a line with quest
visit marks and branches (ticket jvbastel/WOWAI#158)."""

import pytest
from fastapi.testclient import TestClient

import map_web
from draw_fixtures import BORDER_X, CAVE, ISLAND
from fixtures import write_route, write_tile

# A generated route: its line, with a pull point and a quest visit point.
ROUTE = """route drafted_cave 389
point p1 5.00 5.00 0.00 2.00
point pull_pack1 15.00 5.00 0.00 2.00
point q5722_maur_grimtotem 25.00 15.00 0.00 2.00 quest 5722
point p2 25.00 25.00 0.00 2.00
pack pack1 18.00 6.00 0.00
mob 501 11318 # Ragefire Trogg
"""


@pytest.fixture
def routes(tmp_path):
    write_tile(tmp_path / "mmaps", 389, polys=CAVE, next_tile=ISLAND, border_x=BORDER_X)
    write_route(tmp_path / "routes", "drafted_cave", ROUTE)
    return tmp_path / "routes"


@pytest.fixture
def web(routes):
    settings = map_web.Settings(navmesh=routes.parent / "mmaps", routes=routes, exports=routes)
    return TestClient(map_web.create_app(settings))


def draft(web, source):
    response = web.get("/api/maps/drafted_cave/draft", params={"source": source})
    assert response.status_code == 200, response.text
    return response.json()


def plain(x, y, z):
    return {"x": x, "y": y, "z": z, "joined": True, "mark": False, "note": ""}


def test_the_generated_route_loads_as_the_draft_line_with_its_quest_visit_as_a_mark(web):
    lines = draft(web, "route")["lines"]

    assert lines == [[
        {**plain(5.0, 5.0, 0.0), "joined": False},
        plain(15.0, 5.0, 0.0),
        {**plain(25.0, 15.0, 0.0), "mark": True, "note": "q5722_maur_grimtotem"},
        plain(25.0, 25.0, 0.0),
    ]]


# A trace drawn before: a main line with a gap and a checkpoint, and a later
# walk to the quest corner.
TRACE = """# drawn before
trace drafted_cave 389
p 5.00 5.00 0.00
mark 15.00 5.00 0.00 corner of the cave
gap
p 25.00 5.00 0.00
p 25.00 15.00 0.00
walk
p 25.00 20.00 0.00
mark 15.00 25.00 0.00 q5722
"""


def test_an_existing_trace_loads_as_the_draft_line_with_its_marks_gaps_and_walks(web, routes):
    (routes / "drafted_cave.trace.txt").write_text(TRACE)

    lines = draft(web, "trace")["lines"]

    assert lines == [
        [{**plain(5.0, 5.0, 0.0), "joined": False},
         {**plain(15.0, 5.0, 0.0), "mark": True, "note": "corner of the cave"},
         {**plain(25.0, 5.0, 0.0), "joined": False},
         plain(25.0, 15.0, 0.0)],
        [{**plain(25.0, 20.0, 0.0), "joined": False},
         {**plain(15.0, 25.0, 0.0), "mark": True, "note": "q5722"}],
    ]


def test_a_route_with_no_trace_file_has_no_trace_draft(web):
    response = web.get("/api/maps/drafted_cave/draft", params={"source": "trace"})

    assert response.status_code == 404
    assert "no trace file" in response.json()["detail"]
