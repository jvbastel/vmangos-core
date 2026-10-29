#!/usr/bin/env bash
# shellcheck disable=SC2329 # check (lib.sh) runs the functions that the test passes to it.
# Test of the second client copy (spec #204, #207): `wowai/forever-client.sh`.
#
# It makes a fake main client and a fake slot folder in a temporary folder,
# runs setup and install against them, and checks the files. It never touches
# a real client. It needs only bash and coreutils. Run it from any folder:
#
#   wowai/tests/forever-client.test.sh
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
script="$here/../forever-client.sh"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# shellcheck source=lib.sh
. "$here/lib.sh"

main="$tmp/Games/WoW-1.12.1"
mkdir -p "$main/Data" "$main/WDB" "$main/WTF"
echo exe > "$main/WoW.exe"
echo stock > "$main/Data/patch.MPQ"
echo cache > "$main/WDB/wowcache.wdb"
printf 'set realmlist 127.0.0.1:3731' > "$main/realmlist.wtf"
printf 'SET gxResolution "1024x768"\nSET realmList "127.0.0.1:3731"\nSET realmName "WOWAI Test"\n' > "$main/WTF/Config.wtf"
pool="$tmp/slots"
mkdir -p "$pool/3/forever/client"
echo "forever patch" > "$pool/3/forever/client/client-data-patch.MPQ"

export WOWAI_MAIN_CLIENT="$main" WOWAI_POOL_DIR="$pool"
tree_sum() { (cd "$1" && find . -type f -print0 | sort -z | xargs -0 md5sum); }
main_before="$(tree_sum "$main")"
run() { "$script" "$@" > "$tmp/out" 2>&1; }
refused() { if run "$@"; then return 1; fi; }
said() { grep -q "$1" "$tmp/out"; }
not_grep() { ! grep -q "$1" "$2"; }
# refused_with_main <main client setting> <args>: refused, with that main client.
refused_with_main() { local m="$1"; shift; WOWAI_MAIN_CLIENT="$m" refused "$@"; }

copy="$tmp/Games/WoW-1.12.1-forever"
check "setup copies the main client into a missing folder" run setup "$copy" --slot 3
check "the copy has the client files" test -f "$copy/WoW.exe" -a -f "$copy/Data/patch.MPQ"
check "setup writes the marker file" grep -q "slot 3" "$copy/wowai-forever-client.txt"
check "setup points realmlist.wtf at the slot's login port" \
  grep -qx "set realmlist 127.0.0.1:3733" "$copy/realmlist.wtf"
check "setup points Config.wtf at the slot's login port" \
  grep -qx 'SET realmList "127.0.0.1:3733"' "$copy/WTF/Config.wtf"
check "setup drops the saved realm name" not_grep realmName "$copy/WTF/Config.wtf"
check "setup keeps the other settings" grep -qx 'SET gxResolution "1024x768"' "$copy/WTF/Config.wtf"

check "setup refuses the main client folder" refused setup "$main" --slot 3
check "setup refuses the main client by another spelling" refused setup "$main/../WoW-1.12.1/" --slot 3
check "setup refuses a folder inside the main client" refused setup "$main/Interface/copy" --slot 3
check "it names the main client folder" said "inside the main client"
ln -s "$main" "$tmp/main-link"
check "setup refuses the main client through a symlink" refused setup "$tmp/main-link" --slot 3
check "setup refuses a slot that is no number" refused setup "$copy" --slot x
mkdir -p "$tmp/empty"
check "setup refuses a folder that is no client" refused setup "$tmp/empty" --slot 3
check "setup marks an existing client copy" run setup "$copy" --slot 4
check "setup moves an existing copy to another slot" \
  grep -qx "set realmlist 127.0.0.1:3734" "$copy/realmlist.wtf"
run setup "$copy" --slot 3

echo cached > "$copy/WDB/creaturecache.wdb"
check "install copies the slot's client data patch" run install "$copy"
check "the patch is the last patch of the copy" \
  cmp -s "$pool/3/forever/client/client-data-patch.MPQ" "$copy/Data/patch-9.MPQ"
check "install clears the WDB cache" test -d "$copy/WDB" -a -z "$(ls -A "$copy/WDB")"
echo "other patch" > "$tmp/other.MPQ"
check "install takes a patch file" run install "$copy" --patch "$tmp/other.MPQ"
check "the patch file is installed" cmp -s "$tmp/other.MPQ" "$copy/Data/patch-9.MPQ"
check "install refuses a missing patch" refused install "$copy" --slot 2
check "it names the missing patch" said "slots/2/forever/client/client-data-patch.MPQ"

plain="$tmp/Games/plain-copy"
cp -R "$main" "$plain"
check "install refuses a folder with no marker file" refused install "$plain" --slot 3
check "it says why" said "marker"
check "install leaves that folder alone" test ! -e "$plain/Data/patch-9.MPQ" -a -f "$plain/WDB/wowcache.wdb"
check "the main client is unchanged" test "$(tree_sum "$main")" = "$main_before"
cp "$copy/wowai-forever-client.txt" "$main/"
check "install refuses the main client even with a marker file" refused install "$main" --slot 3
check "install refuses the main client by another spelling" refused install "$main/." --slot 3
check "install refuses the main client through a symlink" refused install "$tmp/main-link" --slot 3
check "install refuses a folder inside the main client through a symlink" \
  refused install "$tmp/main-link/Data" --slot 3
# A Windows path names the main client too. Only the refusal runs: it comes
# before any write, and a folder with no marker file is refused anyway.
for win in 'C:\Games\WoW-1.12.1' 'c:/Games/WoW-1.12.1' 'C:\Games\WoW-1.12.1\Data'; do
  check "install refuses the main client as $win" \
    refused_with_main /mnt/c/Games/WoW-1.12.1 install "$win" --slot 3
  check "it names the main client ($win)" said "main client"
done
check "install refuses the main client when the setting is a Windows path" \
  refused_with_main 'C:\Games\WoW-1.12.1' install /mnt/c/Games/WoW-1.12.1 --slot 3
check "it names the main client (Windows setting)" said "main client"
# On WSL, /mnt/c ignores letter case: another case is the same folder.
if [ -d /mnt/c/Games/WoW-1.12.1 ]; then
  check "install refuses the real main client in another letter case" \
    refused_with_main /mnt/c/Games/WoW-1.12.1 install /mnt/c/games/wow-1.12.1 --slot 3
  check "it names the main client (letter case)" said "main client"
fi
check "it names the main client" said "main client"
check "the main client still has no client data patch" test ! -e "$main/Data/patch-9.MPQ" -a -f "$main/WDB/wowcache.wdb"

finish
