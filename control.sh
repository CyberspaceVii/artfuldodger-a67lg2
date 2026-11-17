#!/system/bin/sh
MODDIR=${0%/*}
STATE=/data/adb/artfuldodger
SPOOL=/data/system/artfuldodger_spool
mkdir -p "$STATE" && chmod 0700 "$STATE" || exit 1
pending() {
  count=0
  for f in "$SPOOL"/cap_*.png; do
    [ -f "$f" ] && [ ! -L "$f" ] && count=$((count+1))
  done
  echo "$count"
}
status() {
  echo "Pending images: $(pending)"
  if [ -e "$STATE/disabled" ]; then echo "Capture: disabled"; return; fi
  echo "Capture: enabled"
  live=$(/system/bin/pgrep -x artfuldodgerd 2>/dev/null)
  if [ -z "$live" ]; then echo "Readiness: waiting for supervisor"; return; fi
  if [ ! -f "$STATE/runtime" ]; then echo "Readiness: starting"; return; fi
  report=$(cat "$STATE/runtime")
  field() { printf '%s\n' "$report" | sed -n "s/^$1=//p"; }
  pid=$(field pid)
  boot=$(field boot_id)
  if [ "$live" != "$pid" ] || [ "$boot" != "$(cat /proc/sys/kernel/random/boot_id)" ]; then
    echo "Readiness: starting (previous session report ignored)"; return
  fi
  echo "Readiness: $(field state)"
  echo "This session: $(field saved) saved, $(field failed) failed ($(field requests) requests)"
  echo "Last capture result: $(field last_result)"
  echo "Last capture/save: $(field capture_ms)/$(field save_ms) ms"
}
case "${1:-status}" in
  status) status;;
  off) touch "$STATE/disabled"; /system/bin/pkill -x artfuldodgerd 2>/dev/null; status;;
  on) rm -f "$STATE/disabled"; status;;
  toggle)
    if [ -e "$STATE/disabled" ]; then "$0" on; else "$0" off; fi;;
  retry)
    saved=0; failed=0
    for f in "$SPOOL"/cap_*.png; do
      [ -f "$f" ] && [ ! -L "$f" ] || continue
      /system/bin/timeout 15 /system/bin/sh "$MODDIR/publish.sh" "${f##*/}"
      result=$?
      if [ "$result" = 0 ]; then saved=$((saved+1)); else failed=$((failed+1)); echo "Retry failed: error $result (source retained)"; fi
    done
    echo "Recovery: $saved saved, $failed failed; $(pending) still pending."
    [ "$failed" = 0 ]; exit $?;;
  diagnostics)
    status
    [ ! -f "$STATE/runtime" ] || cat "$STATE/runtime"
    for f in events.previous.log events.log; do
      [ ! -f "$STATE/$f" ] || cat "$STATE/$f"
    done;;
  *) echo "Usage: sh control.sh status|on|off|toggle|retry|diagnostics" >&2; exit 2;;
esac
