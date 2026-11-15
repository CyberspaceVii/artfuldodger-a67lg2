#!/system/bin/sh
# Start the interceptor on boot, then file each capture into the gallery.
MODDIR=${0%/*}
SPOOL=/data/system/artfuldodger_spool
DEST=/data/media/0/Pictures/Screenshots
PUB=/storage/emulated/0/Pictures/Screenshots

until [ "$(getprop sys.boot_completed)" = "1" ]; do sleep 2; done

setsid "$MODDIR/artfuldodgerd" </dev/null >/dev/null 2>&1 &

# system_server can capture but can't write /data/media, so it spools here.
while :; do
  for f in "$SPOOL"/*.png; do
    [ -e "$f" ] || continue
    ts=$(date +%Y%m%d-%H%M%S)_$RANDOM
    out="$DEST/Screenshot_$ts.png"
    mkdir -p "$DEST"
    if cp "$f" "$out"; then
      rm -f "$f"
      chown media_rw:media_rw "$out"
      chmod 0644 "$out"
      restorecon "$out" 2>/dev/null
      content call --uri content://media --method scan_file --arg "$PUB/Screenshot_$ts.png" >/dev/null 2>&1
    fi
  done
  sleep 1
done
