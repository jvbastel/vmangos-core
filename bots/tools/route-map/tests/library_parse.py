"""Parse a trace file (src/bots/RouteTrace.cpp, ParseTrace) or a hand pack
order file (src/bots/RouteGenerator.cpp, ParsePackOrder) with the route
library itself, not with the Python reader: a small program built once per
test session with the host g++. The tests that use it are skipped when there
is no g++."""

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


ORDER_MAIN = r"""
#include "RouteGenerator.h"
#include <cstdio>
#include <iostream>
#include <iterator>
#include <string>

int main()
{
    std::string text((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
    Bots::PackOrderResult result = Bots::ParsePackOrder(text);
    if (!result.ok)
    {
        std::printf("error %s\n", result.error.c_str());
        return 0;
    }
    for (std::uint32_t guid : result.guids)
        std::printf("%u\n", guid);
    return 0;
}
"""


def _build(name: str, main: str, sources: list[str]) -> Path:
    compiler = shutil.which("g++")
    if compiler is None:
        pytest.skip(f"no g++: cannot build the route library's {name}")
    folder = Path(tempfile.mkdtemp(prefix=f"route-map-{name}-"))
    atexit.register(shutil.rmtree, folder, ignore_errors=True)
    (folder / "main.cpp").write_text(main)
    program = folder / name
    paths = [folder / "main.cpp", *(BOTS / source for source in sources)]
    subprocess.run([compiler, "-std=c++14", "-O0", "-I", str(BOTS), *map(str, paths), "-o", str(program)],
                   check=True, capture_output=True, text=True)
    return program


@functools.cache
def _program() -> Path:
    return _build("trace_parse", MAIN, ["RouteTrace.cpp", "DataTokenizer.cpp"])


@functools.cache
def _order_program() -> Path:
    return _build("order_parse", ORDER_MAIN,
                  ["RouteGenerator.cpp", "RouteHandOrder.cpp", "RouteSight.cpp", "Route.cpp", "DataFields.cpp",
                   "DataTokenizer.cpp"])


def library_pack_order(text: str) -> list[int] | str:
    """What ParsePackOrder reads from the text: the guids in order, or the
    error ("line <n>: ...")."""
    out = subprocess.run([str(_order_program())], input=text, check=True, capture_output=True, text=True).stdout
    if out.startswith("error "):
        return out[len("error "):].strip()
    return [int(line) for line in out.split()]


def library_parse(text: str) -> dict:
    """What ParseTrace reads from the text: {"ok": 1, "points", "marks",
    "walks", "gaps"}, or {"ok": 0, "error"}."""
    out = subprocess.run([str(_program())], input=text, check=True, capture_output=True, text=True).stdout
    if out.startswith("ok=0 error="):
        return {"ok": 0, "error": out[len("ok=0 error="):].strip()}
    return {key: int(value) for key, value in (word.split("=") for word in out.split())}
