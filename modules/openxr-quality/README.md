# GXR OpenXR QUALITY 0.1.1 (experimental)

Author: **4uva4ek**. Source layout: [module conventions](../../docs/MODULE-LAYOUT.md).

Separate module holding HEAD_TRACKING QUALITY while an awake headset has a
visible OpenXR application. It is independent of cache bypass, HAL 360 and
pose extrapolation. No prediction offset or pose compensation is added.

The stock perception host's `force-set-mode 1 1` blocks subsequent control-plane
mode requests. `set-mode 1 1` or `set-mode 1 2` releases that latch and refreshes
the camera request. QUALITY requests camera minimum/maximum 60/60 FPS;
EFFICIENCY requests 30/60. These are camera requests, not guaranteed measured
controller update rates or a promise of Quest 3 motion quality.

The command is a Binder shell transaction, not a dumpsys command. The client
uses pipe output descriptors and null optional callbacks; a memfd output failed
on this build. No SELinux policy change is made. Private client-side Parcel ABI,
vendor host and Android policy are guarded by exact hashes. All opaque objects
belong to the helper process; there is no remote memory access or code patch.

## Session behavior

- Awake + visible CPM client + its process maps the system OpenXR runtime:
  hold QUALITY. Polling is approximately once per second.
- Minimize, application exit or sleep: release to the current stock thermal/
  power-saver mode. Returning to the visible app reacquires QUALITY.
- Thermal severity 4 or higher: release; resume only at severity 2 or lower.
  Ordinary camera reduction at severity 3 is intentionally overridden while
  active. CPU/GPU thermal controls remain untouched. This is a polling guard.
- Telemetry/command failure: stop, attempt restoration, disable the module and
  report the error. A host process restart ends this controller session.
- Disable/remove: a running guardian stops the worker and restores its owned
  override. A dead guardian is detected by the worker heartbeat pipe.

## Installation and activation

Build only the automatic package with `python modules/openxr-quality/build.py
--ndk PATH`. The archive contains `disable`. For a new installation, use
KernelSU GXR 1.0.6 or later with the generic GXR lifecycle-contract installer.
The ordinary upstream installer is intentionally rejected.

The unpublished local 0.1.0 does not implement the Manager's update command
contract. An existing 0.1.0 must be stopped with `sh /data/adb/modules/gxr_openxr_quality/auto.sh off`,
then its native `status` must confirm no controller or owned override before
reviewed direct replacement, preserving its `state/` guards and installing OFF.
Do not bypass a Manager restoration refusal or remove a running module.
From 0.1.1, `action.sh off` waits for verified restoration and `action.sh status`
provides a machine-readable safe-update report. The normal Action button remains
a status/recovery report. These script changes do not change the native controller. When replacing an installed
module, disable/restore it and verify no running controller or ownership before
replacing its files. The new package must remain disabled until separately
authorized activation.

Standard KernelSU service startup calls the native controller. It requires
`late_load: true`, `runtime_mode: late-load`, Android boot completion and a
stable completed `/dev/ksu-restart-stage` generation. Cold-boot activation is
blocked. The checkbox is the sole activation preference; Action reports status
and recovery instructions without changing it. No custom KernelSU hooks exist.

Module files and failure guards persist in `/data/adb/modules/gxr_openxr_quality`.
Live state/logs exist only in `/dev/gxr-quality-session`; the vendor latch lives
only in perceptionhost memory. No original file, partition, firmware, calibration,
global setting or thermal override is modified. Full power removal clears the
live override and runtime controller. A later intentional late-load soft reboot
can reactivate the module if the checkbox remains ON. Disable it for persistent
rollback.

Interrupted/failed sessions cannot automatically retry in the same kernel boot.
A confirmed changed kernel boot ID clears previous-boot guards at next startup,
preserving the checkbox. Successful stops do not leave an interruption guard.
Guardian cleanup is best effort: if both processes die or the service cannot
answer, leave OFF and fully remove power/restart. `bin/gxr-quality restore`
clears an orphaned owned override if the service and pinned ABI still match.

## Validation and limits (2026-10-12)

On the connected SM-I610: vendor force accepted; camera minimum 60 request
logged; an ordinary control-plane request was blocked while forced; after
set-mode, ordinary requests reached the plugin again. The assembled helper
passed identity checks and an asleep start/status/stop cycle with no remaining
ownership. Parser tests use retained active Steam Link, idle and paused-gallery
CPM snapshots and test critical-temperature hysteresis/restoration decisions.

Visible app minimize/restore, sustained camera/source rates, subjective motion,
active disable, power-cycle and native throwing have not been validated for
this module. Runtime status checks controller process identity and its latest
report, not an independent readback of the vendor force flag.

Evidence: workspace `diagnostics/tracking-reassessment-20261012/quality-session/`.
