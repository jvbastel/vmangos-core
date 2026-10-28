"""Parse a trace file with the route library itself (src/bots/RouteTrace.cpp,
ParseTrace), not with the Python reader: a small program built once per test
session with the host g++. The tests that use it are skipped when there is
no g++."""

from __future__ import annotations

import atexit
import functools
import shutil
import subprocess
import tempfile
from pathlib import Path

import pytest

BOTS = Path(__file__).resolve().parents[4] / "src" / "bots"

MAIN = r"""
#include "RouteTrace.h"
#include <cstdio>
#include <iostream>
#include <iterator>
#include <string>

int main()
{
    std::string text((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
    Bots::TraceResult result = Bots::ParseTrace(text);
    if (!result.ok)
    {
        std::printf("ok=0 error=%s\n", result.error.c_str());
        return 0;
    }
    unsigned walks = 0, gaps = 0;
    for (std::size_t i = 1; i < result.trace.points.size(); ++i)
    {
        walks += result.trace.points[i].walk;
        gaps += !result.trace.points[i].joined && !result.trace.points[i].walk;
    }
    std::printf("ok=1 points=%zu marks=%zu walks=%u gaps=%u\n", result.trace.points.size(),
        Bots::MarkCount(result.trace), walks, gaps);
    return 0;
}
"""


@functools.cache
def _program() -> Path:
    compiler = shutil.which("g++")
    if compiler is None:
        pytest.skip("no g++: cannot build the route library's trace parser")
    folder = Path(tempfile.mkdtemp(prefix="route-map-trace-parse-"))
    atexit.register(shutil.rmtree, folder, ignore_errors=True)
    (folder / "main.cpp").write_text(MAIN)
    program = folder / "trace_parse"
    sources = [folder / "main.cpp", BOTS / "RouteTrace.cpp", BOTS / "DataTokenizer.cpp"]
    subprocess.run([compiler, "-std=c++14", "-O0", "-I", str(BOTS), *map(str, sources), "-o", str(program)],
                   check=True, capture_output=True, text=True)
    return program


def library_parse(text: str) -> dict:
    """What ParseTrace reads from the text: {"ok": 1, "points", "marks",
    "walks", "gaps"}, or {"ok": 0, "error"}."""
    out = subprocess.run([str(_program())], input=text, check=True, capture_output=True, text=True).stdout
    if out.startswith("ok=0 error="):
        return {"ok": 0, "error": out[len("ok=0 error="):].strip()}
    return {key: int(value) for key, value in (word.split("=") for word in out.split())}
