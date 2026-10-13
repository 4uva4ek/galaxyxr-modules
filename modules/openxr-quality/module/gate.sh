#!/system/bin/sh
# Read-only activation gate. Missing/in-progress state is WAITING, not opt-out.
# The caller owns checkbox preference and must never bypass this predicate.
gate() {
 GATE_REASON='verified late-load soft restart is required'
 GATE_GEN=
 [ "$(getprop sys.boot_completed)" = 1 ] || { GATE_REASON='Android boot is not complete'; return 1; }
 stage=/dev/ksu-restart-stage
 for leaf in "$stage" "$stage/ready" "$stage/generation" "$stage/busy" "$stage/error"; do
  [ ! -L "$leaf" ] || { GATE_REASON='restart gate contains a symlink'; return 1; }
 done
 [ -d "$stage" ] && [ -f "$stage/ready" ] && [ -f "$stage/generation" ] || { GATE_REASON='KernelSU soft-restart ready/generation is absent'; return 1; }
 [ ! -e "$stage/busy" ] && [ ! -e "$stage/error" ] || { GATE_REASON='KernelSU soft restart is busy or has an error'; return 1; }
 [ "$(cat "$stage/ready" 2>/dev/null)" = ready ] || { GATE_REASON='KernelSU ready marker is invalid'; return 1; }
 first=$(cat "$stage/generation" 2>/dev/null) || return 1
 printf '%s\n' "$first" | grep -Eq '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$' || { GATE_REASON='KernelSU restart generation is invalid'; return 1; }
 info=$(timeout 2 /data/adb/ksud debug info 2>/dev/null) || { GATE_REASON='KernelSU runtime mode cannot be verified'; return 1; }
 printf '%s\n' "$info" | grep -qx 'late_load: true' && printf '%s\n' "$info" | grep -qx 'runtime_mode: late-load' || { GATE_REASON='KernelSU is not in verified late-load mode; cold-boot activation is blocked'; return 1; }
 [ ! -e "$stage/busy" ] && [ ! -e "$stage/error" ] && [ -f "$stage/ready" ] && [ "$(cat "$stage/ready" 2>/dev/null)" = ready ] && [ "$(cat "$stage/generation" 2>/dev/null)" = "$first" ] || { GATE_REASON='KernelSU restart generation changed during verification'; return 1; }
 GATE_GEN=$first
 GATE_REASON=
 return 0
}

if gate; then printf "%s\n" "$GATE_GEN"; else printf "WAITING - %s\n" "$GATE_REASON"; exit 1; fi
