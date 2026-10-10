#!/system/bin/sh
MODDIR=${0%/*}
STATE=/dev/gxr_controller_extrapolation
PACKAGE=com.valvesoftware.steamlinkvr
LIBRARY=/system/lib64/libopenxr_android.so
SYSTEM_SERVICE=0
. "$MODDIR/autostart.sh"
