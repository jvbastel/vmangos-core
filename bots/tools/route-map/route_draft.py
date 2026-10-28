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

import trace_file

# A quest visit point of a route file: q<quest>_<name>.
VISIT = re.compile(r"q\d+_")


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
