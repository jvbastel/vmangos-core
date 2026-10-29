#!/usr/bin/env python3
"""Generates the Forever data from the spec file and the stock 1.12.1 DBC files.

    python3 -I forever/generator/generate.py --spec forever/warrior.tsv \\
        --stock <extracted-data>/5875/dbc --out <folder>

Writes <folder>/dbc (the Forever DBC folder, Forever.DataDir), <folder>/client/DBFilesClient (the DBCs of
the client data patch) and <folder>/sql/forever_world.sql (the Forever server tables). The same spec and
stock files give the same bytes. A spec that breaks a rule stops it with an error and no output.
Standard library only, so that the test realm kit runs it with the system Python.
"""
import argparse
import os
import sys

# python3 -I leaves the script folder off the path.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from forever_gen import build, spec  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--spec", required=True, help="the spec file, forever/warrior.tsv")
    parser.add_argument("--stock", required=True, help="the stock DBC folder, <extracted-data>/5875/dbc")
    parser.add_argument("--out", required=True, help="the output folder")
    args = parser.parse_args()
    try:
        build.generate(spec.read(args.spec), args.stock, args.out)
    except spec.SpecError as error:
        sys.exit("generate.py: %s" % error)


main()
