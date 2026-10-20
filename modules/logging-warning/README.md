# Logging Warning

Author: 4uva4ek.

Automatic-only KernelSU GXR module, installed disabled. Raises Android's default
logging threshold to WARNING and shadows existing per-tag `log.tag.*` and
`persist.log.tag.*` overrides with nonpersistent properties. Preserves existing
WARNING/ERROR/FATAL/SILENT thresholds. Discovers new overrides every 60 seconds;
checks disable/remove every five seconds. No log reader or native busy loop.

Only RAM properties named `log.tag` and `log.tag.*` are changed, using
`resetprop -n` without persistent storage or property-service triggers.
Original values and property presence are journaled before mutation. Disable
restores owned values and deletes overrides that did not previously exist.
Concurrent ownership changes fail closed rather than being overwritten.
State/journals persist only in this module's runtime directory. Cold kernel boot
clears stale guards; same-boot failures require inspection or a full reboot.
Full power removal clears all property changes. If the checkbox remains enabled,
the module may apply again when KernelSU starts modules: disable it for recovery.

The checkbox is the sole preference. Action is read-only status/recovery.
Enablement and a module restart require separate user authorization.
`sh /data/adb/modules/gxr_logging_warning/auto.sh disable` disables and restores.
Uninstall uses no Android framework services. KernelSU GXR reviewed ZIP installer
is required; the generic installer deliberately aborts. No system files,
partitions, thermal controls, tracing services or other modules are modified.

Coverage is limited to producers honoring Android liblog properties. Explicit
process minimum priorities, vendor loggers, events/stats, kernel logs, direct
file writes and message formatting before the logger may bypass this threshold.
Existing logcat buffers are not erased. No CPU saving is promised without an
awake workload comparison.

Build: `python modules/logging-warning/build.py`.
Output: `dist/logging-warning/gxr-logging-warning-0.1.0-auto.zip`.
The module has an independent update feed in `updates/logging-warning.json`.

Validation: repository layout/catalogue/contract checks, dedicated package
contract validation and shell syntax. Live manual testing uses a separately
stored temporary copy with no startup hook, followed by property restoration.
See VALIDATION.md for actual results.
