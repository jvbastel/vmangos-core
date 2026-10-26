#!/usr/bin/env bash
# Builds the bot decision library and its GoogleTest tests alone, then runs
# ctest. `wowai/dev test` runs it in the dev container, and the fork CI runs
# it on each push.
#
#   wowai/bots-test.sh <src/bots folder> <work folder> [<bots/data folder>]
#
# It builds a copy of only the src/bots folder, so that the rest of the core
# is not there. A library file that includes a game header then fails to
# build (ADR 0002 of WOWAI). The work folder keeps the build between runs.
# The tests of the data files read the bots/data folder, by default the one
# next to the src folder of <src/bots folder>.
set -euo pipefail

[ "$#" -eq 2 ] || [ "$#" -eq 3 ] || { echo "Usage: $0 <src/bots folder> <work folder> [<bots/data folder>]" >&2; exit 2; }
bots_src="$1"
work="$2"
data="$(cd "${3:-$bots_src/../../bots/data}" 2>/dev/null && pwd)" || data="${3:-$bots_src/../../bots/data}"
# The header-only libraries (nlohmann/json, cpp-httplib), never game headers.
dep_include="$(cd "$bots_src/../../dep/include" && pwd)"
mkdir -p "$work/src" "$work/out"

# Keep the file times, so that only changed files build again.
rsync -a --delete "$bots_src/" "$work/src/"

cd "$work/out"
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_BOT_TESTS=1 -DBOT_DATA_DIR="$data" -DBOT_DEP_INCLUDE_DIR="$dep_include" "$work/src" >/dev/null
# A killed build leaves empty object files, which make takes as up to date.
find "$work/out" -name '*.o' -size 0 -delete
if ! make -j"$(nproc)"; then
  echo "The bot decision library failed to build alone. src/bots must build" >&2
  echo "without the rest of the core: it includes no game header (ADR 0002)." >&2
  exit 1
fi
ctest --output-on-failure
