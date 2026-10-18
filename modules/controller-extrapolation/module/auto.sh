#!/system/bin/sh
MODDIR=${0%/*}
STATE=/dev/gxr_controller_extrapolation
. "$MODDIR/stream-clients.sh"
target_pids() { stream_client_pids; }
LIBRARY=/system/lib64/libopenxr_android.so
SYSTEM_SERVICE=0
. "$MODDIR/autostart.sh"
