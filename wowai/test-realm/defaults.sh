# shellcheck shell=bash
# The defaults of the shared test realm: the one table that realm.sh and the
# tests read. A ticket realm of the check kit sets each of these settings
# itself, from the ticket number.
# shellcheck disable=SC2034 # realm.sh, wowai/dev, and the tests read it.
declare -gA realm_defaults=(
  [WOWAI_REALM_DIR]="$HOME/wowai/test-realm"
  [WOWAI_REALM_PROJECT]=wowai-test-realm
  [WOWAI_REALM_LOGIN_PORT]=3725
  [WOWAI_REALM_WORLD_PORT]=8086
  [WOWAI_REALM_SOAP_PORT]=7879
  [WOWAI_DEV_INSTALL]="$HOME/wowai/dev-install"
)

# The newest local image of a repository, else the fallback image.
newest_image() {
  local repository="$1" fallback="$2" newest
  newest="$(docker images --format '{{.Repository}}:{{.Tag}}' "$repository" | grep -v ':<none>$' | head -n 1 || true)"
  echo "${newest:-$fallback}"
}
