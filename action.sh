#!/system/bin/sh
# Action button: toggle the interceptor off or back on.
MODDIR=${0%/*}
if pgrep -x artfuldodgerd >/dev/null 2>&1; then
  pkill -x artfuldodgerd
  echo "Restricted-screen screenshots disabled."
else
  setsid "$MODDIR/artfuldodgerd" </dev/null >/dev/null 2>&1 &
  echo "Restricted-screen screenshots enabled."
fi
