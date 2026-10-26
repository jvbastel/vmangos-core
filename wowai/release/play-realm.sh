# Play realm helpers of `release-core`, sourced. They need: play_realm,
# records, services (the Compose services a release sets), and die().
#
# The chat service repository has a copy of this file in scripts/. Change
# both together.

# shellcheck shell=bash
# The sourcing script sets play_realm, records, and services.
# shellcheck disable=SC2154
revision_label=org.opencontainers.image.revision

# The docker command for the play realm. The user runs that Compose project
# from Windows, so a run from WSL with /mnt/c paths would change its paths.
play_docker() {
  if [ -n "${WOWAI_PLAY_DOCKER:-}" ]; then
    echo "$WOWAI_PLAY_DOCKER"
  elif [[ "$play_realm" == /mnt/[a-z]/* ]] && command -v docker.exe >/dev/null; then
    echo docker.exe
  else
    echo docker
  fi
}

play_compose() { (cd "$play_realm" && "$(play_docker)" compose "$@"); }

# Reads or sets the `image` and `pull_policy` of a service in the play
# realm's compose.yaml. It keeps a YAML anchor on the image line, the line
# endings (CRLF from Windows), and every other line.
compose_awk() {
  local file="$play_realm/compose.yaml"
  [ -f "$file" ] || die "no compose.yaml in $play_realm"
  awk "$@" '
    {
      line = $0; cr = ""
      if (line ~ /\r$/) { cr = "\r"; line = substr(line, 1, length(line) - 1) }
      if (line ~ /^[^ #]/) { in_services = (line ~ /^services:/); current = "" }
      else if (in_services && line ~ /^  [A-Za-z0-9_.-]+:[ \t]*(#.*)?$/) {
        current = line; sub(/^  /, "", current); sub(/:.*/, "", current)
      }
      mine = in_services && current == service
      if (mine && line ~ /^    image:/) {
        anchor = ""; value = line; sub(/^    image:[ \t]*/, "", value)
        if (value ~ /^&/) { anchor = value; sub(/[ \t].*/, "", anchor); sub(/^&[^ \t]+[ \t]+/, "", value) }
        sub(/[ \t]+#.*/, "", value)
        found++
      }
      if (mode == "image" && mine && line ~ /^    image:/) print value
      if (mode == "policy" && mine && line ~ /^    pull_policy:/) { v = line; sub(/^    pull_policy:[ \t]*/, "", v); sub(/[ \t]+#.*/, "", v); print v }
      if (mode == "set") {
        if (mine && line ~ /^    pull_policy:/) next
        if (mine && line ~ /^    image:/) {
          print "    image: " (anchor == "" ? "" : anchor " ") image cr
          if (policy != "") print "    pull_policy: " policy cr
          next
        }
        print $0
      }
    }
    END { if (found != 1) exit 3 }
  ' "$file"
}

compose_image() {
  compose_awk -v mode=image -v service="$1" || die "found no single image line of service '$1' in $play_realm/compose.yaml"
}

compose_pull_policy() {
  compose_awk -v mode=policy -v service="$1" || die "found no single image line of service '$1' in $play_realm/compose.yaml"
}

# set_compose_image <service> <image> [<pull policy>]. No policy removes it.
set_compose_image() {
  local file="$play_realm/compose.yaml"
  compose_awk -v mode=set -v service="$1" -v image="$2" -v policy="${3:-}" >"$file.wowai-new" ||
    { rm -f "$file.wowai-new"; die "found no single image line of service '$1' in $file"; }
  mv "$file.wowai-new" "$file"
}

# The commit an image was built from, from its OCI revision label. Empty if
# the image or the label is missing.
image_revision() {
  docker image inspect --format "{{index .Config.Labels \"$revision_label\"}}" "$1" 2>/dev/null | sed 's/^<no value>$//' || true
}

# Makes a backup with the backup-now command of the `database-backup`
# service and prints the new backup file name.
backup_now() {
  local dir="$play_realm/storage/database/backups" before after file
  mkdir -p "$dir"
  before="$(ls -1 "$dir")"
  play_compose run --rm -e DB_DUMP_CRON= -e DB_DUMP_ONCE=true database-backup >&2 ||
    die "the backup failed. Nothing changed."
  after="$(ls -1 "$dir")"
  file="$(comm -13 <(sort <<<"$before") <(sort <<<"$after") | tail -n 1)"
  [ -n "$file" ] || die "the backup made no new file in $dir. Nothing changed."
  echo "$file"
}

# Restores a backup file of storage/database/backups/ with the services
# that use the database stopped. The caller starts them again.
restore_backup() {
  local file="$1"
  shift
  play_compose stop "$@"
  play_compose run --rm database-backup restore --target /backup "$file"
}

# write_record <kind> <commit> <previous commit> <backup> <migrations> <name
# of an associative array: service -> new image>. Writes the release record
# that `rollback` reads.
write_record() {
  local kind="$1" commit="$2" previous="$3" backup="$4" migrations="$5"
  local -n new_images="$6"
  local service file
  mkdir -p "$records"
  file="$records/$kind-$(date -u +%Y%m%dT%H%M%S.%NZ).env"
  {
    printf 'COMMIT=%q\n' "$commit"
    printf 'PREVIOUS_COMMIT=%q\n' "$previous"
    printf 'BACKUP=%q\n' "$backup"
    printf 'MIGRATIONS=%q\n' "$migrations"
    for service in "${services[@]}"; do
      printf 'PREVIOUS_IMAGE_%s=%q\n' "$service" "$(compose_image "$service")"
      printf 'PREVIOUS_POLICY_%s=%q\n' "$service" "$(compose_pull_policy "$service")"
      printf 'NEW_IMAGE_%s=%q\n' "$service" "${new_images[$service]}"
    done
  } >"$file"
  echo "Release record: $file" >&2
}

# The newest release record of <kind> that was not rolled back.
latest_record() {
  find "$records" -maxdepth 1 -name "$1-*.env" 2>/dev/null | sort | tail -n 1
}

# rollback_record <record>: sets the previous images of the release back and
# marks the record as rolled back. If the release ran a migration (the record
# lists one, or "unknown"), it first stops the services in RESTORE_STOP and
# restores the backup made before the release. It prints "restored" then.
# The caller starts the services.
# shellcheck disable=SC2153 # The record sets COMMIT, MIGRATIONS, and BACKUP.
rollback_record() {
  local record="$1" service image_var policy_var
  # shellcheck disable=SC1090
  . "$record"
  echo "Rolling back $COMMIT (record $(basename "$record"))." >&2
  if [ -n "${MIGRATIONS// /}" ]; then
    echo "It ran migrations ($MIGRATIONS). Restoring the backup $BACKUP." >&2
    restore_backup "$BACKUP" "${RESTORE_STOP[@]}" >&2
    echo restored
  fi
  for service in "${services[@]}"; do
    image_var="PREVIOUS_IMAGE_$service" policy_var="PREVIOUS_POLICY_$service"
    set_compose_image "$service" "${!image_var}" "${!policy_var}"
  done
  mv "$record" "$record.rolled-back"
}
