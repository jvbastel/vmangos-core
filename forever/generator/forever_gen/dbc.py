"""Read and write the WDBC files of the 1.12.1 client: a header, rows of 4-byte fields, then a block of
NUL-terminated strings that the string fields point into."""
import struct

HEADER = "<4s4I"


class Dbc:
    """A DBC file in memory. rows: lists of the raw unsigned 32-bit fields (a float field stays raw)."""

    def __init__(self, fields, rows, strings):
        self.fields = fields
        self.rows = rows
        self.strings = bytearray(strings)
        self._added = {}

    @classmethod
    def read(cls, path):
        with open(path, "rb") as file:
            data = file.read()
        magic, records, fields, size, string_size = struct.unpack_from(HEADER, data, 0)
        if magic != b"WDBC" or size != fields * 4:
            raise ValueError("%s: not a WDBC file of 4-byte fields" % path)
        body = 20 + records * size
        rows = [list(struct.unpack_from("<%dI" % fields, data, 20 + i * size)) for i in range(records)]
        return cls(fields, rows, data[body:body + string_size])

    def text(self, offset):
        return self.strings[offset:self.strings.index(b"\0", offset)].decode("utf-8")

    def add_text(self, value):
        """The offset of a new string at the end of the string block. The stock strings keep their
        offsets, so the stock rows stay byte for byte."""
        if value == "":
            return 0
        if value not in self._added:
            self._added[value] = len(self.strings)
            self.strings += value.encode("utf-8") + b"\0"
        return self._added[value]

    def by_id(self):
        return {row[0]: row for row in self.rows}

    def copy(self):
        return Dbc(self.fields, [list(row) for row in self.rows], bytes(self.strings))

    def write(self, path):
        with open(path, "wb") as file:
            file.write(struct.pack(HEADER, b"WDBC", len(self.rows), self.fields, self.fields * 4, len(self.strings)))
            for row in self.rows:
                if len(row) != self.fields:
                    raise ValueError("%s: a row of %d fields, not %d" % (path, len(row), self.fields))
                file.write(struct.pack("<%dI" % self.fields, *row))
            file.write(bytes(self.strings))
