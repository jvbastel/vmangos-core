"""The draft line of the route drawing (ticket jvbastel/WOWAI#158): load the
line of the generated route or of an existing trace, so that the player
edits a line instead of drawing from nothing. map_web.create_app adds the
call with add_routes; the page part is map_draft.js.

    GET /api/maps/{route}/draft?source=route   the line of the route file (the generator's draft)
    GET /api/maps/{route}/draft?source=trace   the line of <route>.trace.txt, with its marks and walks

A draft is a list of lines. The first line is the main line; each later
line is a branch: a later walk of the trace, which `.route build` walks out
and back from the nearest spot of the main line. A point is
{x, y, z, joined, mark, note}: `joined` false after a gap, `mark` for a
quest visit mark or another checkpoint.
"""

from __future__ import annotations

import re
from pathlib import Path

from fastapi import FastAPI, HTTPException
from fastapi.responses import FileResponse
from pydantic import BaseModel

import trace_file

HERE = Path(__file__).resolve().parent

# A quest visit point of a route file: q<quest>_<name>.
VISIT = re.compile(r"q\d+_")


class DraftPoint(BaseModel):
    """A point of a drawn line, as the page sends it."""
    x: float
    y: float
    z: float
    joined: bool = True  # joined to the point before by a floor path; false after a gap
    mark: bool = False  # a quest visit mark or another checkpoint
    note: str = ""


def draft_trace(name, map_id, main, branches, path):
    """The trace of a drawn line: the main line, then each branch as a later
    walk. Each joined piece is the floor path `path(a, b)` between its two
    points (a floor_path.Piece). Returns the trace and the warnings of the
    pieces: {"piece": k} on the main line, {"branch": n, "piece": k} on a
    branch."""
    trace = trace_file.Trace(name, map_id)
    warnings = []
    for number, line in enumerate([main, *branches]):
        for k, point in enumerate(line):
            here = (point.x, point.y, point.z)
            if k and point.joined:
                before = line[k - 1]
                piece = path((before.x, before.y, before.z), here)
                trace.points += [trace_file.TracePoint(*p) for p in piece.points[1:-1]]
                where = {"branch": number, "piece": k} if number else {"piece": k}
                warnings += [{**w, **where} for w in piece.warnings]
            trace.points.append(trace_file.TracePoint(
                *here, joined=bool(trace.points) and k > 0 and point.joined,
                walk=bool(trace.points) and k == 0 and number > 0,
                mark=point.mark, note=trace_file.clean_note(point.note)))
    return trace, warnings


def point_view(x, y, z, joined=True, mark=False, note=""):
    return {"x": round(x, 2), "y": round(y, 2), "z": round(z, 2), "joined": joined, "mark": mark, "note": note}


def route_lines(route) -> list[list[dict]]:
    """The line of a route file as one draft line. A quest visit point
    becomes a mark, named after the point."""
    line = []
    for point in route.line:
        visit = bool(VISIT.match(point.name))
        line.append(point_view(point.x, point.y, point.z, joined=bool(line), mark=visit,
                               note=point.name if visit else ""))
    return [line] if line else []


def trace_lines(trace: trace_file.Trace) -> list[list[dict]]:
    """A trace as draft lines: the main line, then one branch per later walk."""
    lines: list[list[dict]] = []
    for point in trace.points:
        if not lines or point.walk:
            lines.append([])
        lines[-1].append(point_view(point.x, point.y, point.z, joined=point.joined and not point.walk,
                                    mark=point.mark, note=point.note))
    return lines


def add_routes(app: FastAPI, settings, load_route):
    @app.get("/map_draft.js")
    def script():
        return FileResponse(HERE / "map_draft.js", media_type="text/javascript")

    @app.get("/api/maps/{name}/draft")
    def draft(name: str, source: str = "route"):
        route = load_route(name)
        if source == "route":
            return {"source": source, "lines": route_lines(route)}
        if source == "trace":
            path: Path = settings.routes / f"{name}.trace.txt"
            if not path.is_file():
                raise HTTPException(404, f"no trace file {path}")
            try:
                trace = trace_file.parse(path.read_text())
            except trace_file.TraceFileError as error:
                raise HTTPException(422, f"{path}: {error}")
            return {"source": source, "file": str(path), "lines": trace_lines(trace)}
        raise HTTPException(422, "source is route or trace")
