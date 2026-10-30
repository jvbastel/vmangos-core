"""Writes an MPQ archive of format 0, the format that the 1.12.1 client reads, for the client data patch.

Layout: the 32-byte header, the files, the hash table, then the block table. Each file is cut into
sectors of 4096 bytes, each sector compressed with zlib (or stored raw when that does not make it
smaller), behind a table of sector offsets, as in Blizzard's own patch archives. The archive also holds
a "(listfile)" with the file names, for MPQ tools. The hash and block tables are encrypted, as the
format requires. The same files give the same bytes.
"""
import struct
import zlib

SECTOR_SIZE_SHIFT = 3  # sector = 512 << 3 = 4096 bytes
SECTOR = 512 << SECTOR_SIZE_SHIFT
HEADER_SIZE = 32
FILE_COMPRESS, FILE_EXISTS = 0x00000200, 0x80000000
COMPRESSION_ZLIB = 0x02
HASH_EMPTY = 0xFFFFFFFF
LISTFILE = "(listfile)"


def _crypt_table():
    table = [0] * 0x500
    seed = 0x00100001
    for index1 in range(0x100):
        index2 = index1
        for _ in range(5):
            seed = (seed * 125 + 3) % 0x2AAAAB
            high = (seed & 0xFFFF) << 16
            seed = (seed * 125 + 3) % 0x2AAAAB
            table[index2] = high | (seed & 0xFFFF)
            index2 += 0x100
    return table


CRYPT = _crypt_table()
HASH_OFFSET, HASH_A, HASH_B, HASH_KEY = 0, 1, 2, 3


def hash_string(name, kind):
    """The MPQ hash of a name (case and slash do not matter)."""
    seed1, seed2 = 0x7FED7FED, 0xEEEEEEEE
    for char in name.upper().replace("/", "\\").encode("ascii"):
        seed1 = (CRYPT[(kind << 8) + char] ^ (seed1 + seed2)) & 0xFFFFFFFF
        seed2 = (char + seed1 + seed2 + (seed2 << 5) + 3) & 0xFFFFFFFF
    return seed1


def encrypt(words, key):
    seed = 0xEEEEEEEE
    out = []
    for word in words:
        seed = (seed + CRYPT[0x400 + (key & 0xFF)]) & 0xFFFFFFFF
        out.append(word ^ ((key + seed) & 0xFFFFFFFF))
        key = ((((~key) << 0x15) + 0x11111111) | (key >> 0x0B)) & 0xFFFFFFFF
        seed = (word + seed + (seed << 5) + 3) & 0xFFFFFFFF
    return out


def _pack_file(data):
    """The stored form of a file: the sector offset table, then each sector."""
    sectors = []
    for start in range(0, len(data), SECTOR):
        raw = data[start:start + SECTOR]
        packed = bytes([COMPRESSION_ZLIB]) + zlib.compress(raw, 9)
        sectors.append(packed if len(packed) < len(raw) else raw)
    offsets = [4 * (len(sectors) + 1)]
    for sector in sectors:
        offsets.append(offsets[-1] + len(sector))
    return struct.pack("<%dI" % len(offsets), *offsets) + b"".join(sectors)


def write(path, files):
    """Writes the archive at `path`. files: {archive path, for example "DBFilesClient\\Talent.dbc": bytes}.
    Each file must hold at least one byte."""
    files = dict(files)
    for name, data in files.items():
        if not data:
            raise ValueError("%s: an MPQ file needs at least one byte" % name)
    files[LISTFILE] = "".join(name + "\r\n" for name in sorted(files) if name != LISTFILE).encode("ascii")
    names = sorted(files, key=lambda n: n.upper())

    hash_size = 16
    while hash_size < len(names) * 2:
        hash_size *= 2

    body = bytearray()
    blocks = []
    for name in names:
        data = files[name]
        stored = _pack_file(data)
        blocks.append((HEADER_SIZE + len(body), len(stored), len(data), FILE_COMPRESS | FILE_EXISTS))
        body += stored

    hashes = [[HASH_EMPTY, HASH_EMPTY, HASH_EMPTY, HASH_EMPTY] for _ in range(hash_size)]
    for block, name in enumerate(names):
        slot = hash_string(name, HASH_OFFSET) % hash_size
        while hashes[slot][3] != HASH_EMPTY:
            slot = (slot + 1) % hash_size
        # name hash A, name hash B, locale 0 (neutral) and platform 0 in one word, block index
        hashes[slot] = [hash_string(name, HASH_A), hash_string(name, HASH_B), 0, block]

    hash_words = encrypt([w for entry in hashes for w in entry], hash_string("(hash table)", HASH_KEY))
    block_words = encrypt([w for entry in blocks for w in entry], hash_string("(block table)", HASH_KEY))
    hash_pos = HEADER_SIZE + len(body)
    block_pos = hash_pos + 16 * hash_size
    archive_size = block_pos + 16 * len(blocks)
    header = struct.pack("<4sIIHHIIII", b"MPQ\x1a", HEADER_SIZE, archive_size, 0, SECTOR_SIZE_SHIFT,
                         hash_pos, block_pos, hash_size, len(blocks))
    with open(path, "wb") as file:
        file.write(header)
        file.write(bytes(body))
        file.write(struct.pack("<%dI" % len(hash_words), *hash_words))
        file.write(struct.pack("<%dI" % len(block_words), *block_words))
