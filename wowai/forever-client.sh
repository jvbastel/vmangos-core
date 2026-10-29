#!/usr/bin/env bash
# The second client copy for the Forever rules (spec #204, #207).
#
# The player plays the Forever rules from a second copy of the 1.12.1 client.
# The main client stays vanilla: this script never writes into it.
#
#   wowai/forever-client.sh setup <folder> --slot <N>
#       Marks <folder> as the Forever client: writes the marker file
#       wowai-forever-client.txt and points its realm list (realmlist.wtf and
#       WTF/Config.wtf) at the login port of test realm slot N (3730 + N).
#       When <folder> does not exist, it first copies the main client into it
#       (a copy: the main client stays as it is).
#
#   wowai/forever-client.sh install <folder> [--slot <N> | --patch <file>]
#       Copies the client data patch into <folder>/Data/patch-9.MPQ and clears
#       the WDB cache of <folder>. The patch is <file>, or the one that the
#       Forever generator made for slot N (default: the slot of the marker
#       file): <pool>/<N>/forever/client/client-data-patch.MPQ.
#
# Both refuse the main client folder. install also refuses a folder with no
# marker file. Close the client of <folder> before an install.
#
# Settings (environment):
#   WOWAI_MAIN_CLIENT  the main client folder. Default: /mnt/c/Games/WoW-1.12.1
#   WOWAI_POOL_DIR     the folder of the test realm slots, as in
#                      wowai/test-realm/realm.sh. Default: ~/wowai/slots
set -euo pipefail

marker_name=wowai-forever-client.txt
# The 1.12.1 client loads Data/patch.MPQ, then Data/patch-?.MPQ in name order;
# a later archive wins. patch-9 sorts after the stock patch-2.
patch_name="patch-9.MPQ"
main_client="${WOWAI_MAIN_CLIENT:-/mnt/c/Games/WoW-1.12.1}"
pool_dir="${WOWAI_POOL_DIR:-$HOME/wowai/slots}"

die() {
  echo "forever-client.sh: $*" >&2
  exit 1
}

usage() {
  sed -n '2,/^set -euo/p' "$0" | sed '$d; s/^# \{0,1\}//' >&2
  exit 2
}

# Refuse the main client folder and the folders in it, by any spelling.
refuse_main() {
  local folder main
  folder="$(realpath -m "$1")"
  main="$(realpath -m "$main_client")"
  [ "$folder" != "$main" ] ||
    die "$1 is the main client folder ($main_client). It stays vanilla: use a second client copy."
  case "$folder/" in
    "$main"/*) die "$1 is inside the main client folder ($main_client). It stays vanilla." ;;
  esac
}

check_slot() {
  [[ "$1" =~ ^[0-9]+$ ]] || die "the slot must be a number, not '$1'"
}

# setup <folder> --slot <N>
setup() {
  local folder="" slot=""
  while [ "$#" -gt 0 ]; do
    case "$1" in
      --slot) [ "$#" -ge 2 ] || usage; slot="$2"; shift 2 ;;
      -*) usage ;;
      *) [ -z "$folder" ] || usage; folder="$1"; shift ;;
    esac
  done
  [ -n "$folder" ] && [ -n "$slot" ] || usage
  check_slot "$slot"
  refuse_main "$folder"
  if [ ! -e "$folder" ]; then
    [ -f "$main_client/WoW.exe" ] || die "no client at $main_client to copy (set WOWAI_MAIN_CLIENT)"
    echo "Copying the main client $main_client to $folder..."
    rm -rf "$folder.partial"
    cp -R "$main_client" "$folder.partial"
    mv "$folder.partial" "$folder"
  fi
  [ -f "$folder/WoW.exe" ] || die "$folder holds no WoW.exe: it is no client folder"

  local port=$((3730 + slot))
  printf 'This folder is the Forever client (spec #204): slot %s, login port %s.\nwowai/forever-client.sh install puts the client data patch into Data/%s.\n' \
    "$slot" "$port" "$patch_name" > "$folder/$marker_name"
  printf 'set realmlist 127.0.0.1:%s' "$port" > "$folder/realmlist.wtf"
  local config="$folder/WTF/Config.wtf"
  if [ -f "$config" ]; then
    # The saved realm name belongs to another realm: drop it, so the client
    # asks. Keep the line ends of the file.
    sed -i -e "s/^SET realmList \"[^\"]*\"/SET realmList \"127.0.0.1:$port\"/" -e '/^SET realmName /d' "$config"
    grep -q '^SET realmList ' "$config" || printf 'SET realmList "127.0.0.1:%s"\n' "$port" >> "$config"
  fi
  echo "$folder is the Forever client of slot $slot (login port $port)."
}

# install <folder> [--slot <N> | --patch <file>]
install() {
  local folder="" slot="" patch=""
  while [ "$#" -gt 0 ]; do
    case "$1" in
      --slot) [ "$#" -ge 2 ] || usage; slot="$2"; shift 2 ;;
      --patch) [ "$#" -ge 2 ] || usage; patch="$2"; shift 2 ;;
      -*) usage ;;
      *) [ -z "$folder" ] || usage; folder="$1"; shift ;;
    esac
  done
  [ -n "$folder" ] || usage
  refuse_main "$folder"
  [ -f "$folder/$marker_name" ] ||
    die "$folder has no marker file $marker_name: run 'wowai/forever-client.sh setup' on it first"
  if [ -z "$patch" ]; then
    [ -n "$slot" ] || slot="$(sed -n 's/.*: slot \([0-9]*\),.*/\1/p' "$folder/$marker_name")"
    check_slot "$slot"
    patch="$pool_dir/$slot/forever/client/client-data-patch.MPQ"
  fi
  [ -f "$patch" ] ||
    die "no client data patch at $patch (acquire the slot with --forever to generate it)"
  mkdir -p "$folder/Data"
  cp "$patch" "$folder/Data/$patch_name.partial"
  mv "$folder/Data/$patch_name.partial" "$folder/Data/$patch_name"
  rm -rf "$folder/WDB"
  mkdir "$folder/WDB"
  echo "Installed $patch as $folder/Data/$patch_name and cleared $folder/WDB."
}

[ "$#" -ge 1 ] || usage
command="$1"
shift
case "$command" in
  setup) setup "$@" ;;
  install) install "$@" ;;
  *) usage ;;
esac
