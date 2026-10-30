#!/usr/bin/env python3
"""Generates the Forever data from the spec folder and the stock 1.12.1 DBC files.

    python3 -I forever/generator/generate.py --spec forever/spec \\
        --stock <extracted-data>/5875/dbc --out <folder>

Writes <folder>/dbc (the Forever DBC folder, Forever.DataDir), <folder>/client/DBFilesClient (the DBCs of
the client data patch), <folder>/client/client-data-patch.MPQ (the client data patch) and
<folder>/sql/forever_world.sql (the Forever server tables). The same spec and
stock files give the same bytes. A spec that breaks a rule stops it with an error and no output.
Standard library only, so that the test realm kit runs it with the system Python.
"""
import argparse
import os
import sys

# python3 -I leaves the script folder off the path.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from forever_gen import build, markers, spec  # noqa: E402

# The src folder of the core fork, whose code decides which spell icons are markers.
CORE_SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "src")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--spec", required=True, help="the spec folder forever/spec (every *.tsv in it), or one spec file")
    parser.add_argument("--stock", required=True, help="the stock DBC folder, <extracted-data>/5875/dbc")
    parser.add_argument("--out", required=True, help="the output folder")
    parser.add_argument("--core-src", default=CORE_SRC,
                        help="the src folder of the core, for the spell marker icons (default: this fork's)")
    args = parser.parse_args()
    try:
        build.generate(spec.read(args.spec), args.stock, args.out, markers.scan(args.core_src))
    except spec.SpecError as error:
        sys.exit("generate.py: %s" % error)
    except (OSError, ValueError) as error:
        sys.exit("generate.py: cannot read the input: %s" % error)


main()
