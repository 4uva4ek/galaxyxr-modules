# HAL Cache Bypass 0.1.1

> **Deprecated:** included in [controller-suite](../controller-suite/README.md). Update to KernelSU GXR 1.0.11 or later and install the suite. Removed from Sources; historical releases remain available.

Author: **4uva4ek**.

[Release and automatic ZIP](https://github.com/4uva4ek/galaxyxr-modules/releases/tag/hal-cache-bypass/v0.1.1).
See [validation and remaining limits](../../docs/HAL-CACHE-BYPASS-2026-10-12.md).

Available through the repository's **Sources** catalogue and its own update feed.
Use a GXR Manager with the generic GXR lifecycle-contract installer; older builds
with a fixed five-module allowlist cannot install this module. The GXR installer
verifies and installs the package disabled without executing `customize.sh`.
The ordinary installer refusal remains for installers that can rewrite startup
metadata. Enabling and soft restart remain separate actions.

This independent module bypasses the cached-pose reuse branch in Samsung's
controller predictor. It changes one instruction in the controller service's
private RAM. It adds no pose/velocity postprocessing, timestamp offset, HAL query
horizon or application hook. It does not modify an on-disk system library,
firmware, partition or calibration.
HAL360, HAL Native and NativeCopy remain separate application source choices.

This changes the native predictor path. The bypass still uses the native
predictor's special-path cache behavior, other timing limits, smoothing and
filters. Recomputing instead of returning the normal cached result intentionally
can change the returned pose. It does not guarantee that every HAL
request produces a pose at the requested epoch, eliminate all motion corrections,
or establish correct native object throwing.

## Activation and cold-boot protection

Only automatic packages are built; replacement is installed **disabled**. Disable
and restore an existing enabled instance before replacement. Enabling requires
the user's separate authorization.

KernelSU's checkbox is the only activation preference. Both normal service
startup and the explicit `action.sh on` path require all of:

- Android reports boot complete and `/dev` is tmpfs.
- `/data/adb/ksud debug info` succeeds and reports exactly `late_load: true` and
  `runtime_mode: late-load`.
- `/dev/ksu-restart-stage/ready` contains `ready`, `generation` is a stable UUID,
  and `busy` and `error` are absent. Symlinked gate entries are rejected.

Absent or in-progress gate state reports **DISABLED — Waiting** and preserves the
checkbox. There is no time-based assumption that boot completion makes the patch
safe to activate. The native helper independently enforces the same gate, including
direct `hold`, `run` and `on` commands. The original standalone diagnostic helper
is not packaged here.

The worker and native owner bind the completed restart generation. A new restart,
missing ready marker, busy/error state or generation change stops and restores
the patch. Native owner death invokes an independent restoration guardian; shell
supervisor death sends the owner SIGTERM. A health or activation failure writes
the KernelSU disable marker and preserves interruption evidence for that kernel
boot. A confirmed changed boot ID clears old guards without changing ON or OFF.

## Status and recovery

Action is an English status and recovery report. It checks the real branch and
module ownership; a checked box is not proof of a live patch.

```sh
sh /data/adb/modules/gxr_hal_cache_bypass/action.sh status
sh /data/adb/modules/gxr_hal_cache_bypass/action.sh off
sh /data/adb/modules/gxr_hal_cache_bypass/action.sh on
```

`off` disables the preference, waits for its exact native owner, and verifies
restoration. It can terminate a stuck owner to invoke its guardian. A lease alone
never authorizes a generic restore because it is written before activation.
Unknown or external owners
are reported, never adopted. Restoration failures remain explicit.

If restoration is uncertain, leave this module disabled and fully remove headset
power before rebooting. Power removal destroys the current private RAM patch.
Cold-start activation is blocked by the actual KernelSU mode gate; a later
late-load soft restart may activate a still-enabled module, so disabling is the
persistent rollback.

## Native guard and build

Pinned `libsctrlclient.so` SHA256:
`36120385f25c61f5c2d832d2ac2ebf2179dea1ba8142beeb1001994b7f0d22c0`.
VA `0x723cc`: stock `0x5400050a` becomes `0xd503201f`. The helper verifies model
SM-I610, exact service PID/start identity, private RX mapping and full RX bytes
apart from this known instruction. It stops every service thread, rejects PCs
inside the patch word, preserves its neighboring instruction, and reads back
activation/restoration. Original files are never written.

```sh
python modules/hal-cache-bypass/build.py --ndk /path/to/ndk/28.2.13676358
```

Output: `dist/hal-cache-bypass/gxr-hal-cache-bypass-0.1.1-auto.zip`, containing the
disable marker. Use the compatible GXR Sources installer described above; the
ordinary installer remains deliberately blocked. The packaged helper's `--selftest` uses only an owned
child's executable memory and does not activate the service patch.
