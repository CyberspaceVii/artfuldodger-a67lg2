#!/system/bin/sh
[ "$(getprop ro.product.device)" = A67L_Gen2 ] || abort "Fail! This module is for the Foxxd A67L Gen 2."
[ "$KSU" = true ] || abort "Fail! KernelSU Next is required."

ui_print "The Artful Dodger CyberspaceVII"
ui_print " "
ui_print "Enabling restricted-screen screenshots:"

ok=1
[ -f "$MODPATH/artfuldodgerd" ] || ok=0
[ -f "$MODPATH/zygisk/arm64-v8a.so" ] || ok=0
[ -f "$MODPATH/publish.sh" ] || ok=0
[ -f "$MODPATH/control.sh" ] || ok=0
chmod 0755 "$MODPATH/artfuldodgerd" "$MODPATH/service.sh" "$MODPATH/publish.sh" "$MODPATH/control.sh" "$MODPATH/action.sh" "$MODPATH/uninstall.sh" 2>/dev/null

[ "$ok" = 1 ] || abort "Missing required files; installation aborted."
ui_print "Success!"
