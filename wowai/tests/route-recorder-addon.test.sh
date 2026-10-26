#!/usr/bin/env bash
# Checks the RouteRecorder addon (wowai/addons/RouteRecorder) in Lua 5.0,
# the Lua of the 1.12 client: the syntax with luac, then a stub of the client
# API that clicks the buttons and feeds it the server lines.
#
#   LUA50=<folder with bin/lua and bin/luac of Lua 5.0> wowai/tests/route-recorder-addon.test.sh
#
# Build Lua 5.0.3 from https://www.lua.org/ftp/lua-5.0.3.tar.gz with `make`.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
addon="$here/../addons/RouteRecorder"
[ -n "${LUA50:-}" ] && [ -x "$LUA50/bin/lua" ] || { echo "Set LUA50 to a Lua 5.0 build folder." >&2; exit 2; }
case "$("$LUA50/bin/lua" -v 2>&1)" in *"Lua 5.0"*) ;; *) echo "$LUA50/bin/lua is not Lua 5.0." >&2; exit 2 ;; esac

for file in "$addon"/*.lua; do
  "$LUA50/bin/luac" -p "$file"
  echo "PASS luac 5.0 syntax: $(basename "$file")"
done
# The client has none of these in Lua 5.0.
if grep -nE '#[A-Za-z_(]|string\.gmatch|\.\.\.\s*\)|select\(' "$addon"/*.lua; then
  echo "FAIL the addon uses Lua 5.1 syntax" >&2
  exit 1
fi
cd "$here/route-recorder-addon"
"$LUA50/bin/lua" harness.lua "$addon"
