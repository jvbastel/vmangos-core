"""The rows in encrypted client data (encrypted.tsv, US42 of spec #225): left out and listed.

Build 1.60.1.70685 of the Forever client hides some profession items in data sections with an unknown key.
The spec has no row for them. The generator refuses an item row whose Forever data (foreverItem) is one of
them, and writes them into encrypted-list.tsv, so that the post-launch refresh knows what to try again.
"""
from collections import namedtuple

from .spec import SpecError, rows_of

COLUMNS = ("foreverItem", "name", "used", "notes")
LIST_COLUMNS = ("foreverItem", "name", "used")
Encrypted = namedtuple("Encrypted", "forever_item name used notes")


def read_rows(path, lines):
    return {"encrypted": [Encrypted(forever_item=int(raw["foreverItem"]), name=raw["name"], used=raw["used"],
                                    notes=raw["notes"]) for _, raw in rows_of(path, lines, COLUMNS)]}


def check(spec):
    """No encrypted row twice, and no item row of the spec takes its Forever data from one."""
    seen = set()
    for row in spec.encrypted:
        if row.forever_item in seen:
            raise SpecError("encrypted row %s: %d is in encrypted.tsv twice" % (row.name, row.forever_item))
        seen.add(row.forever_item)
    for item in spec.items:
        if item.forever_item in seen:
            raise SpecError("item %s (%d): its foreverItem %d is in encrypted client data (encrypted.tsv)"
                            % (item.name, item.entry, item.forever_item))


def list_text(spec):
    """The encrypted list (encrypted-list.tsv): a header, the column line and one row per item, by item."""
    lines = ["# The profession items in encrypted client data that the spec leaves out (forever/spec/encrypted.tsv).",
             "# The post-launch refresh tries them again.", "\t".join(LIST_COLUMNS)]
    lines += ["%d\t%s\t%s" % (row.forever_item, row.name, row.used)
              for row in sorted(spec.encrypted, key=lambda r: r.forever_item)]
    return "\n".join(lines) + "\n"
