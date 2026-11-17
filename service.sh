#!/system/bin/sh
MODDIR=${0%/*}
STATE=/data/adb/artfuldodger
mkdir -p "$STATE"
chmod 0700 "$STATE"
until [ "$(getprop sys.boot_completed)" = 1 ]; do sleep 2; done
# Restart the interceptor after a worker disconnect.
while [ ! -e "$MODDIR/disable" ] && [ ! -e "$MODDIR/remove" ]; do
  if [ ! -e "$STATE/disabled" ] && ! /system/bin/pgrep -x artfuldodgerd >/dev/null 2>&1; then
    "$MODDIR/artfuldodgerd" "$MODDIR/publish.sh" </dev/null >>"$STATE/service.log" 2>&1 &
  fi
  # Bound diagnostics on this low-storage device.
  if [ -f "$STATE/service.log" ] && [ "$(wc -c < "$STATE/service.log")" -gt 65536 ]; then
    mv -f "$STATE/service.log" "$STATE/service.previous.log"
  fi
  sleep 3
done
/system/bin/pkill -x artfuldodgerd 2>/dev/null
