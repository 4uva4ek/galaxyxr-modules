#!/system/bin/sh
STATE=/dev/gxr_hal_prediction_30
PACKAGE=com.sec.android.secxrcontrollerservice
LIBRARY=/system/lib64/libsecxrcontrollerservice_jni.xrcontroller.samsung.so
fail() {
  echo "ERROR: $*"
  : > "$MODDIR/disable"
  printf '%s\n' "$*" > "$MODDIR/last-error"
  exit 1
}
[ "$(id -u)" = 0 ] || { echo 'ERROR: root required'; exit 1; }
awk '$2 == "/dev" && $3 == "tmpfs" {ok=1} END {exit !ok}' /proc/mounts || fail '/dev is not tmpfs'
[ ! -L "$STATE" ] || fail 'Unsafe state directory'
umask 077
mkdir -p "$STATE"
identity() {
  TARGET=$(pidof "$PACKAGE" || true)
  case "$TARGET" in '') IDENTITY=; return 0;; *[!0-9]*) echo 'ERROR: ambiguous controller service'; return 1;; esac
  STARTED=$(sed 's/.*) //' "/proc/$TARGET/stat" 2>/dev/null | awk '{print $20}')
  [ -n "$STARTED" ] || return 1
  IDENTITY=$TARGET:$STARTED
}
checksums() {
  (cd "$MODDIR" && sha256sum -c SHA256SUMS) > "$STATE/checksums.log" 2>&1
}
