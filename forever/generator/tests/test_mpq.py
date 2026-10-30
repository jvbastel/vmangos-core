"""The MPQ writer of the client data patch, read back with an independent MPQ reader (mpyq)."""
import os
import random

import mpyq

from forever_gen import mpq

# 1.12 sectors are 4096 bytes (512 << 3).
SECTOR = 4096


def read_back(path):
    archive = mpyq.MPQArchive(str(path), listfile=True)
    return archive, {name.decode(): archive.read_file(name.decode()) for name in archive.files}


def test_an_archive_gives_back_each_file_by_its_path(tmp_path):
    rng = random.Random(207)
    files = {
        "DBFilesClient\\Talent.dbc": b"WDBC" + bytes(range(256)) * 40,          # compressible, many sectors
        # A last sector of noise does not compress and is stored raw. (mpyq cannot read a raw sector
        # that is not the last one: it takes every full sector for compressed. The client and StormLib
        # compare the stored size with the sector size.)
        "DBFilesClient\\Spell.dbc": b"\0" * 3 * SECTOR + bytes(rng.getrandbits(8) for _ in range(17)),
        "DBFilesClient\\TalentTab.dbc": b"short",                               # less than one sector
    }
    path = tmp_path / "patch.MPQ"
    mpq.write(path, files)
    archive, read = read_back(path)
    assert read == files
    assert archive.header["magic"] == b"MPQ\x1a"
    assert archive.header["format_version"] == 0  # the 1.12 client reads format 0 only
    assert archive.header["archive_size"] == os.path.getsize(path)


def test_a_file_name_is_found_in_any_case_and_with_either_slash(tmp_path):
    path = tmp_path / "patch.MPQ"
    mpq.write(path, {"DBFilesClient\\Talent.dbc": b"x" * 100})
    archive = mpyq.MPQArchive(str(path), listfile=False)
    assert archive.read_file("dbfilesclient\\TALENT.DBC") == b"x" * 100
    assert archive.read_file("DBFilesClient\\Spell.dbc") is None


def test_many_files_fill_the_hash_table_without_loss(tmp_path):
    files = {"DBFilesClient\\File%03d.dbc" % i: (b"%d " % i) * (i + 1) for i in range(300)}
    path = tmp_path / "patch.MPQ"
    mpq.write(path, files)
    assert read_back(path)[1] == files


def test_the_same_files_give_the_same_bytes(tmp_path):
    files = {"b\\two.dbc": b"2" * 5000, "a\\one.dbc": b"1" * 10}
    mpq.write(tmp_path / "one.MPQ", files)
    mpq.write(tmp_path / "two.MPQ", dict(reversed(list(files.items()))))
    assert (tmp_path / "one.MPQ").read_bytes() == (tmp_path / "two.MPQ").read_bytes()
