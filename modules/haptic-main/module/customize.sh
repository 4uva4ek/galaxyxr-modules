#!/system/bin/sh
[ "$KSU" = true ] || abort 'KernelSU required'
[ "$ARCH" = arm64 ] || abort 'ARM64 required'
STOCK=/product/priv-app/secxrcontroller/secxrcontroller.apk
EXPECTED=8130b5542c36235eb1e951b9245684299d6a7ddd1a8c95f231e8241e477673cb
ACTUAL=$(sha256sum "$STOCK" | cut -d ' ' -f 1)
[ "$ACTUAL" = "$EXPECTED" ] || abort 'Unsupported Samsung controller APK; refusing installation'
(cd "$MODPATH" && sha256sum -c SHA256SUMS) || abort 'Module checksum failure'
[ "$(sha256sum /system/lib64/libsecxrcontrollerservice_jni.xrcontroller.samsung.so | cut -d ' ' -f 1)" = 80d45131558b28107cf7e3c9a2c483bce4e2207b934e4f25e951f0fc79d1cd36 ] || abort 'Unsupported JNI library'
set_perm_recursive "$MODPATH" 0 0 0755 0644
set_perm "$MODPATH/bin/gxr-main" 0 0 0755
set_perm "$MODPATH/action.sh" 0 0 0755
ui_print 'Installed inactive. Action toggles the in-memory hook.'
ui_print 'No startup scripts or system mounts. Reboot restores stock.'
