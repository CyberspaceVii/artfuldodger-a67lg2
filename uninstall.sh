#!/system/bin/sh
# Stop interception and remove private state and pending images.
/system/bin/pkill -x artfuldodgerd 2>/dev/null
rm -rf /data/system/artfuldodger_spool

rm -rf /data/adb/artfuldodger
