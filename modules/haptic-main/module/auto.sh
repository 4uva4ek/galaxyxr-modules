#!/system/bin/sh
MODDIR=${0%/*}
STATE=/dev/gxr_haptic_main
PACKAGE=com.sec.android.secxrcontrollerservice
LIBRARY=/system/lib64/libsecxrcontrollerservice_jni.xrcontroller.samsung.so
SYSTEM_SERVICE=1
SOFT_RESTART_AWARE=1
TARGET_READY_TIMEOUT=20
. "$MODDIR/autostart.sh"
