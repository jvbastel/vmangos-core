"""The client data patch: one MPQ with the changed client tables, for the second client copy (#207).

It packs every file of the client/ output folder (client/DBFilesClient/*.dbc) under its path in the
client, for example DBFilesClient\\Talent.dbc. The install step of the second client copy
(wowai/forever-client.sh) copies it into the Data folder of that copy under a patch name that loads last.
"""
import os

from . import mpq

PATCH_NAME = "client-data-patch.MPQ"


def write(client_folder):
    """Writes client_folder/PATCH_NAME from the files in client_folder. Returns its path."""
    path = os.path.join(client_folder, PATCH_NAME)
    files = {}
    for root, _, names in os.walk(client_folder):
        for name in names:
            full = os.path.join(root, name)
            if full == path:
                continue
            with open(full, "rb") as file:
                files[os.path.relpath(full, client_folder).replace(os.sep, "\\")] = file.read()
    mpq.write(path, files)
    return path
