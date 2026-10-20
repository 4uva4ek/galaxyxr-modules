#!/system/bin/sh
# Run only against an inert /data/local/tmp copy; never against installed modules.
MODDIR=${0%/*}
case "$MODDIR" in /data/local/tmp/gxr-logging-warning-test-*) ;; *) exit 2 ;; esac
cd "$MODDIR" || exit 2
snapshot() {
    getprop | grep -E '^\[(persist\.)?log\.tag(\.|\])' | sort
}
snapshot > before.txt
cleanup() {
    sh "$MODDIR/logging-ctl.sh" off
    wait "$worker" 2>/dev/null
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM
for script in "$MODDIR/"*.sh; do sh -n "$script" || exit 1; done
timeout 30 sh "$MODDIR/logging-ctl.sh" run > run.txt 2>&1 &
worker=$!
sleep 4
sh "$MODDIR/logging-ctl.sh" status
[ -f "$MODDIR/runtime/active" ] || exit 1
[ "$(getprop log.tag)" = W ] || exit 1
[ "$(getprop log.tag.APM_AudioPolicyManager)" = W ] || exit 1
[ "$(getprop log.tag.SpaceflingerInputDispatcher)" = W ] || exit 1
token="warning-smoke-$$"
log -p i -t GXRWarningProbe "$token-INFO"
log -p w -t GXRWarningProbe "$token-WARNING"
log -p e -t GXRWarningProbe "$token-ERROR"
sleep 1
logcat -d -v brief -s GXRWarningProbe > probe.txt
if grep -F "$token-INFO" probe.txt; then
    echo 'FAIL: INFO bypassed threshold.'
    exit 1
fi
grep -F "$token-WARNING" probe.txt || exit 1
grep -F "$token-ERROR" probe.txt || exit 1
sh "$MODDIR/logging-ctl.sh" off || exit 1
wait "$worker" || exit 1
snapshot > after.txt
cmp before.txt after.txt || { echo 'FAIL: properties not exactly restored.'; exit 1; }
[ ! -s "$MODDIR/runtime/journal" ] || exit 1
trap - EXIT
echo 'PASS: INFO suppressed; WARNING/ERROR retained; all logging properties restored exactly.'
