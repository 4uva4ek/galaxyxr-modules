# Manual and automatic packages

Haptic MAIN 0.3.0 and Controller Extrapolation 0.2.0 each ship as two ZIPs:

- `-manual.zip`: Action only; no startup script or background worker.
- `-auto.zip`: automatic activation through KernelSU's standard `service.sh` stage.

Choose one variant per module. They use the same module ID and native patcher;
do not install both together. The automatic variant initializes its saved auto
setting on its first service start. Later failures or manual disabling remain
disabled across reboots. Switching variants requires a clean replacement of the
module directory; do not unpack a manual ZIP over an auto directory with old hooks.
The modules do not obtain root themselves or change DFRoot settings.

## Select a mode

The automatic package starts on KernelSU's next service stage. To explicitly
reenable it after a detected failure:

```sh
su -c 'sh /data/adb/modules/gxr_haptic_main/auto.sh enable'
su -c 'sh /data/adb/modules/gxr_controller_extrapolation/auto.sh enable'
```

Use `auto.sh status` to see the saved setting and last failure. Use `auto.sh disable`
to turn off automatic activation. Disabling auto stops its worker but leaves an
already-applied memory patch active. To disable auto and restore stock immediately:

```sh
su -c 'sh /data/adb/modules/gxr_haptic_main/action.sh off'
su -c 'sh /data/adb/modules/gxr_controller_extrapolation/action.sh off'
```

The original `action.sh on|off|toggle|status` commands remain available. KernelSU's
Action button still toggles the patch manually. Manual `on`, `off`, or `toggle`
first disables autostart in the automatic package; `status` does not change it. If the target
process has exited, `off` may report that it cannot find the process; auto has
already been disabled, and the exited process's memory patch is gone.

## When KernelSU starts the modules

On ordinary KernelSU startup, `service.sh` resumes only modules whose auto option
is enabled. Each worker waits for `sys.boot_completed=1` before inspecting or
patching a target. It never holds up KernelSU's boot stage.

KernelSU itself is not modified. If a temporary-root flow skips the standard
service stage, merely obtaining root will not launch the package. Its `service.sh`
can be started explicitly in that session without running a soft reboot or other
KernelSU stages. It respects the saved setting and never reenables a failed module.

## Stop on the first detected failure

- Auto waits for the target process and its runtime library, then applies the
  existing guarded `on` operation once per process lifetime, never `toggle`.
- A checksum, firmware, process-identity, patch, or readback failure removes the
  persistent enabled flag. An app that has not loaded OpenXR yet remains in the
  waiting state; this is not treated as a patch failure.
- Before patching, a persistent `pending` marker is written and synchronized.
  It remains until the same process survives for 30 seconds and a fresh status
  check confirms the patch. An interrupted attempt blocks the next automatic
  start, including after reboot.
- A controller-service exit or restart disables haptic autostart. Steam Link can
  restart normally after the 30-second observation period; an earlier exit is
  treated conservatively as a failure and disables extrapolation autostart.
- Status is checked every 30 seconds after activation. Removing/disabling the
  module stops its worker and clears the saved auto setting when detected.

These checks detect process/patch failures; they cannot prove motion quality or
identify every kernel or hardware failure. A crash after the observation period
may not be attributable to a module. Full reboot alone is therefore **not rollback
while autostart is enabled**: disable auto first, then reboot. A stale worker lock
is cleared by reboot; do not force-kill a patch operation.

## Persistent writes and validation

Only the module's own `autostart/` directory stores persistent preferences,
`pending`, and failure text. Worker locks and runtime logs remain in `/dev`
(tmpfs). There are no system-partition writes, firmware changes, mount replacements,
SELinux changes, or changes to root acquisition. Installation still uses the
reviewed manual deployment path because the Samsung installer can write metadata.

Host tests exercise lifecycle, failure latching, duplicate activation prevention,
and recovery guards with simulated processes. They do not replace headset testing
of the new automatic mode. Earlier headset validation applies to the unchanged
native patch operations, not to the new startup integration.
