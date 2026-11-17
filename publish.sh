#!/system/bin/sh
# Exit codes are shared with source/protocol.h.
set -u
fail() { echo "$2" >&2; exit "$1"; }
NAME=${1:-}
case "$NAME" in cap_*.png) ;; *) fail 20 "Invalid pending image";; esac
CORE=${NAME#cap_}; CORE=${CORE%.png}
case "$CORE" in ''|*[!0-9_]*) fail 20 "Invalid pending image";; esac
[ ${#NAME} -lt 96 ] || fail 20 "Invalid pending image"
user_ok() {
  [ "$(cmd activity get-current-user 2>/dev/null)" = 0 ] || fail 21 "Unsupported foreground user"
  [ "$(getprop sys.user.0.ce_available)" = true ] || fail 22 "Credential storage locked"
}
user_ok
STATE=/data/adb/artfuldodger
mkdir -p "$STATE" || fail 26 "Cannot create state directory"
chmod 0700 "$STATE" || fail 26 "Cannot secure state directory"
exec 9>"$STATE/publish.lock" || fail 26 "Cannot open publication lock"
attempt=0
until /system/bin/flock -x -n 9 9>&9; do
  attempt=$((attempt+1))
  [ "$attempt" -lt 5 ] || fail 29 "Publication busy"
  sleep 1
done
SPOOL=/data/system/artfuldodger_spool
SRC="$SPOOL/$NAME"
[ -f "$SRC" ] && [ ! -L "$SRC" ] && [ -s "$SRC" ] || fail 20 "Missing or invalid pending image"
DEST=/data/media/0/Pictures/Screenshots
PUB=/storage/emulated/0/Pictures/Screenshots
mkdir -p "$DEST" || fail 26 "Cannot create screenshot directory"
OUT="$DEST/Screenshot_${NAME#cap_}"
if [ -e "$OUT" ] || [ -L "$OUT" ]; then
  [ -f "$OUT" ] && [ ! -L "$OUT" ] && cmp -s "$SRC" "$OUT" || fail 28 "Destination conflict; source retained"
else
  TMP=$(mktemp "$DEST/.artfuldodger_XXXXXX") || fail 26 "Cannot allocate destination file"
  trap 'rm -f "$TMP"' EXIT HUP INT TERM
  cp "$SRC" "$TMP" || fail 26 "Copy failed; source retained"
  chown media_rw:media_rw "$TMP" && chmod 0644 "$TMP" && restorecon "$TMP" || fail 26 "Cannot set screenshot permissions"
  user_ok
  ln "$TMP" "$OUT" || fail 28 "Cannot publish without overwriting; source retained"
fi
user_ok
content call --uri content://media --method scan_file --arg "$PUB/Screenshot_${NAME#cap_}" >/dev/null 2>&1 || fail 27 "Gallery scan failed; source retained"
ROW=$(content query --uri content://media/external/images/media --projection _id --where "_display_name='Screenshot_${NAME#cap_}' AND relative_path='Pictures/Screenshots/'" 2>&1) || fail 27 "Gallery query failed; source retained"
case "$ROW" in 'Row: '*_id=*) ;; *) fail 27 "Image saved but not indexed; source retained";; esac
rm -f "$SRC" || fail 26 "Image saved; source cleanup failed"
