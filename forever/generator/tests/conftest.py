"""Shared fixtures of the generator tests.

`out` is one generator run on the spec folder for the whole session: the tests only read the generated files, so
one run is enough (#240 review M6: each test file had its own copy, each a full generator run).
"""
import pytest

from test_pipeline import generate


@pytest.fixture(scope="session")
def out(tmp_path_factory):
    return generate(tmp_path_factory.mktemp("forever"))
