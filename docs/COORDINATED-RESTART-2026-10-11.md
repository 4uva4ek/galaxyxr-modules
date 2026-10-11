# Framebuffer10 and the KernelSU coordinated restart API

## Follow-up: 0.3.1 tracing and Whitening 0.1.3

Framebuffer10 0.3.1 keeps the 0.3.0 native binary and shader byte-for-byte. Each
restart hook additionally records its generation, uptime, shell trace and exit
code in `/dev/gxr-framebuffer10-hook-<phase>.log`, outside the native-owned
session directory. KernelSU 1.0.3 also captures generic hook output and clears
stale transaction errors at the beginning of a new requested restart.

Whitening's worker previously kept checking the HAL's System object across a
planned Android restart. The observed invalid-pointer failure caused verified
stock restoration and quarantine; its original per-query timestamp was not
recorded, so the precise transition cannot be reconstructed. The missing
planned-restart handling is a confirmed lifecycle gap. The new
`emulated-soft-reboot.sh` requests worker shutdown and verifies restoration
before Android stop, without changing the checkbox. The next ordinary service
stage starts a fresh owner. Unexpected loss or calibration failures still
restore and quarantine; no calibration check is bypassed. Native errors now log
the pointer address/value and target; the misleading stale errno suffix is gone.

Device testing on kernel boot `72b37fd9-d6a5-47ef-9946-bcf8198d35d0` completed
three successful soft restarts with phase tracing, including two with active
Whitening and KernelSU 1.0.3. Framebuffer, Whitening and Haptics were verified
active afterward. Velocity's existing worker survived and waited for an OpenXR
app. Expected owner/guardian processes were present without duplicate owners.
The initial started-hook exit 1 did not recur; its exact failing command was
not logged in 0.3.0 and remains unproven. Do not claim that tracing alone proves
a root-cause fix. Full cold boot and visual quality are not validated by these
soft-restart checks.

Framebuffer10 0.3.0 requires the new KernelSU GXR 1.0.2 restart stages. It no longer
activates from `service.sh`. The reason is documented in the KernelSU repository
at `docs/COORDINATED-RESTART.md`: restarting zygote after ordinary modules had
started invalidated Haptics' patched process and triggered its crash guard.

The module implements `restart-prepare.sh`, `restart-stopped.sh`,
`restart-started.sh` and `restart-abort.sh` through `restart-hooks.sh`:

1. Prepare verifies checksums, pinned files and baseline, or requests a verified
   existing guardian's handoff to KernelSU.
2. Stopped waits for the old guardian/lease to finish. A new independent native
   guardian verifies SurfaceFlinger is stopped, arms temporary oneshot, bind
   mounts the separately stored color-corrected compositor library, sets
   `cpm.10bit_framebuffers=true`, and acknowledges this transaction's readiness.
3. KernelSU performs the sole successful-path Android start. The native guardian
   verifies the loaded replacement and 10-bit allocations, resets the selector,
   removes the bind mount and verifies framework/output health. Original files
   are never overwritten. The loaded mapping and allocated buffers remain live.
4. Started requires actual native ENABLED status. Only then may KernelSU dispatch
   ordinary modules. Abort disables the participant, requests rollback and checks
   the original startup baseline before a KernelSU baseline start.

When failure occurs while still stopped, the native owner clears temporary
changes and returns startup control to KernelSU without independently starting
Android. Once Android is running, the existing recovery path may restart it to
restore the stock display. Independent guardian/power-cycle recovery is retained;
the one-restart guarantee describes successful activation, not failure recovery.

The shader payload and HDR limitations are unchanged: corrected 10-bit SDR is not
full HDR. Native and shell mocks do not validate visual quality or prove a real
cold boot and repeated headset restart sequence. Packages remain automatic-only
and installed disabled. An older KernelSU cannot execute this protocol; the
module's service status report does not pretend that it activated.

Haptics 0.5.5 and Velocity 0.3.3 are not changed by this integration. Their standard
restart hooks still handle existing workers; startup ordering removes the extra
unannounced restart. Other modules retain their native fixes and controls.
