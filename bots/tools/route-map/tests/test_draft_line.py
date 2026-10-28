"""The draft line of the route drawing through the web API: load the line
of the generated route or of an existing trace, and save a line with quest
visit marks and branches (ticket jvbastel/WOWAI#158)."""

import math

import pytest
from fastapi.testclient import TestClient

import map_web
import trace_file
from library_parse import library_parse
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


def click(x, y, z, **flags):
    return {"x": x, "y": y, "z": z, **flags}


def save(web, points, branches):
    response = web.post("/api/maps/drafted_cave/trace", json={"points": points, "branches": branches})
    assert response.status_code == 200, response.text
    return response.json()


# The main line goes round the wall block to (5, 25), with a quest visit mark
# on the way. The branch leaves the main line at (25, 5) for the ledge at the
# top of the ramp, with a quest visit mark there.
MAIN = [click(5, 5, 0), click(25, 5, 0), click(25, 15, 0, mark=True, note="q5723  troggs # here"),
        click(5, 25, 0)]
BRANCH = [click(25, 5, 0), click(45, 5, 8, mark=True, note="q5722_maur_grimtotem")]


def test_saving_a_line_with_a_mark_and_a_branch_writes_marks_and_a_later_walk(web, routes):
    saved = save(web, MAIN, [BRANCH])

    text = (routes / "drafted_cave.trace.txt").read_text()
    trace = trace_file.parse(text)
    walk = next(i for i, p in enumerate(trace.points) if p.walk)
    main, branch = trace.points[:walk], trace.points[walk:]
    assert [(p.x, p.y, p.z, p.note) for p in trace.points if p.mark] == [
        (25.0, 15.0, 0.0, "q5723 troggs here"), (45.0, 5.0, 8.0, "q5722_maur_grimtotem")]
    assert [(p.x, p.y, p.z) for p in (main[0], main[-1], branch[0], branch[-1])] == [
        (5.0, 5.0, 0.0), (5.0, 25.0, 0.0), (25.0, 5.0, 0.0), (45.0, 5.0, 8.0)]
    assert all(p.joined for p in main[1:]) and all(p.joined for p in branch[1:])
    assert sum(p.walk for p in trace.points) == 1
    assert len(branch) > 6  # the floor path up the ramp, not a straight line
    # points a few yards apart (on the flat; the ramp climbs 8 yards in 10)
    assert all(math.dist((a.x, a.y), (b.x, b.y)) <= 3.5 for line in (main, branch)
               for a, b in zip(line, line[1:]))
    assert saved["points"] == len(trace.points)
    assert library_parse(text) == {"ok": 1, "points": len(trace.points), "marks": 2, "walks": 1, "gaps": 0}


def test_saving_keeps_a_gap_of_the_draft_line_unjoined(web, routes):
    gapped = [click(5, 5, 0), click(25, 5, 0), click(25, 25, 0, joined=False), click(5, 25, 0)]

    save(web, gapped, [])

    text = (routes / "drafted_cave.trace.txt").read_text()
    points = trace_file.parse(text).points
    after = next(i for i, p in enumerate(points) if not p.joined and i > 0)
    assert (points[after].x, points[after].y) == (25.0, 25.0)
    assert (points[after - 1].x, points[after - 1].y) == (25.0, 5.0)  # no floor path over the gap
    assert library_parse(text)["gaps"] == 1


def test_a_trace_loaded_as_the_draft_saves_back_with_its_marks_and_walks(web, routes):
    (routes / "drafted_cave.trace.txt").write_text(TRACE)
    lines = draft(web, "trace")["lines"]

    save(web, lines[0], lines[1:])

    again = trace_file.parse((routes / "drafted_cave.trace.txt").read_text())
    loaded = trace_file.parse(TRACE)
    assert [(p.x, p.y, p.z, p.note) for p in again.points if p.mark] == [
        (p.x, p.y, p.z, p.note) for p in loaded.points if p.mark]
    assert sum(p.walk for p in again.points) == 1
    assert sum(not p.joined for p in again.points[1:]) == 2  # the gap and the walk


def test_the_page_loads_the_draft_script_after_the_drawing_script(web):
    page = web.get("/").text
    script = web.get("/map_draft.js")

    assert page.index('<script src="/map_draw.js">') < page.index('<script src="/map_draft.js">')
    assert script.status_code == 200
    assert script.headers["content-type"].startswith("text/javascript")
    assert "Load trace" in script.text
