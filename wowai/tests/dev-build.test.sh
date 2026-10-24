#!/usr/bin/env bash
# End-to-end test of the dev build: `wowai/dev build`.
#
# It clones the fork into a temporary tree, builds it in its own dev
# container and volume, and removes them again at the end. A first build
# takes a few minutes. Run it from any folder:
#
#   wowai/tests/dev-build.test.sh
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
dev="$here/../dev"
repo="$(git -C "$here" rev-parse --show-toplevel)"

tmp="$(mktemp -d)"
export WOWAI_SRC="$tmp/src"
export WOWAI_DEV_INSTALL="$tmp/install"

failures=0
pass() { echo "PASS: $*"; }
fail() { echo "FAIL: $*"; failures=$((failures + 1)); }
check() { local name="$1"; shift; if "$@"; then pass "$name"; else fail "$name"; fi; }

cleanup() {
  "$dev" clean >/dev/null 2>&1 || true
  rm -rf "$tmp"
}
trap cleanup EXIT

git clone --quiet --shared "$repo" "$WOWAI_SRC"
git -C "$WOWAI_SRC" config core.autocrlf false

echo "First dev build..."
"$dev" build

check "the install folder has mangosd and realmd" \
  test -x "$WOWAI_DEV_INSTALL/bin/mangosd" -a -x "$WOWAI_DEV_INSTALL/bin/realmd"
check "the source tree stays clean (the patches are not in it)" \
  test -z "$(git -C "$WOWAI_SRC" status --porcelain)"

check "the install folder belongs to the calling user" \
  test -O "$WOWAI_DEV_INSTALL/bin/mangosd"

deploy_dir="${WOWAI_DEPLOY_DIR:-/mnt/c/Games/vmangos-deploy}"
deploy_base="$(tr -d '\r' < "$deploy_dir/docker/server/Dockerfile" | sed -n 's/^FROM \([^ ]*\) AS build$/\1/p')"
base_layer="$(docker image inspect --format '{{index .RootFS.Layers 0}}' "$deploy_base")"
dev_container="$(docker ps --quiet --filter "label=wowai.dev.src=$WOWAI_SRC")"
dev_image="$(docker container inspect --format '{{.Image}}' "$dev_container")"
check "the dev container is built on the vmangos-deploy build stage base ($deploy_base)" \
  bash -c "docker image inspect --format '{{json .RootFS.Layers}}' '$dev_image' | grep -q '$base_layer'"
check "the dev container bind-mounts the source read-only" \
  bash -c "! '$dev' shell touch /src/wowai-dev-write-test 2>/dev/null"

check "the copy has no .git" "$dev" shell test ! -e /build/core/.git
src_time="$(stat -c %Y "$WOWAI_SRC/src/game/Objects/Player.cpp")"
copy_time="$("$dev" shell stat -c %Y /build/core/src/game/Objects/Player.cpp | tr -d '\r')"
check "the copy keeps the file times" test "$src_time" = "$copy_time"
check "the copy has the vmangos-deploy patches" \
  "$dev" shell grep -q "vmangos-deploy" /build/core/src/shared/Config/Config.cpp
check "the source tree does not have the patches" \
  bash -c "! grep -q vmangos-deploy '$WOWAI_SRC/src/shared/Config/Config.cpp'"

cache() { "$dev" shell grep -E "^$1:" /build/out/CMakeCache.txt | tr -d '\r'; }
check "no link time optimization" \
  bash -c "[[ '$(cache CMAKE_INTERPROCEDURAL_OPTIMIZATION)' =~ =(OFF|0)$ ]]"
check "debug symbols on" \
  bash -c "file -L '$WOWAI_DEV_INSTALL/bin/mangosd' | grep -q 'with debug_info'"
check "no extractors" bash -c "[[ '$(cache BUILD_EXTRACTORS)' =~ =(OFF|0)$ ]] && [ ! -e '$WOWAI_DEV_INSTALL/bin/Extractors' ]"

echo "Second dev build after a one-file change..."
echo "// wowai dev build test" >> "$WOWAI_SRC/src/game/PlayerBots/PlayerBotMgr.cpp"
start=$SECONDS
"$dev" build | tee "$tmp/second-build.log"
echo "Second build took $((SECONDS - start)) seconds."
compiled="$(grep -c 'Building CXX object' "$tmp/second-build.log" || true)"
check "the second build compiles only the changed file (compiled $compiled)" \
  bash -c "[ '$compiled' = 1 ] && grep -q 'PlayerBotMgr.cpp.o' '$tmp/second-build.log'"
check "the second build reconfigures nothing" \
  bash -c "! grep -q 'Configuring done' '$tmp/second-build.log'"

if [ "$failures" -ne 0 ]; then
  echo "$failures check(s) failed."
  exit 1
fi
echo "All checks passed."
