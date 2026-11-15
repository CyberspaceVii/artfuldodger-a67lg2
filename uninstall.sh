#!/system/bin/sh
# Stop the daemon (grab releases, keys revert) and clear the spool.
# Systemless - nothing was touched, so nothing to restore.
pkill -x artfuldodgerd 2>/dev/null
rm -rf /data/system/artfuldodger_spool
