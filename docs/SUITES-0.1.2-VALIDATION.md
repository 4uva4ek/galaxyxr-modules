# Suite 0.1.2 validation - 2026-10-18

Controller Suite owns controller-log-relief (the existing guarded HAL LogLevel=0
mechanism). Steam Link Suite owns openxr-priority, a profile-based scheduler for
visible third-party OpenXR applications. Steam Link is the only initial profile;
unknown apps stay unchanged. This does not implement OpenXR performance BOOST.

Both suites built with the existing NDK 28/29 toolchain; new native code passes
-Wall -Wextra -Werror. Policy/ownership fixtures cover exact runtime mappings,
UID/shared-UID rejection, CPM visibility and sleep, unknown profiles, external -20,
realtime exclusion, duplicate ownership, restoration on background, exit/PID reuse,
external priority conflicts, and failures after a priority write. Both ZIPs pass
identity/hash/disabled-state and shell checks; internal component contracts, layout
and catalogue checks pass. No real process priorities were changed by host tests.

After explicit installation, enablement and soft-restart authorization, both old
0.1.0 suites were disabled and restored, and both 0.1.2 ZIPs were installed disabled.
Both were then enabled and ksud soft-reboot completed. Android boot completed with
the same kernel boot ID. Controller Suite verified all live components, including
HAL LogLevel=0 and tail-velocity preflight. Steam Link Suite initially reported
headset asleep and zero owned priority threads. After the user opened Steam Link,
its runtime/pose hooks were active and the priority profile reported six owned
threads at nice=-20. The ownership journal and a process-priority snapshot were
retained locally. Extrapolation stayed at its default OFF/stock setting.

These are startup/live-state observations, not a smoothness/latency measurement.
Background/disable restoration is covered by host policy tests; the initial asleep
state alone does not establish a device-side active-to-asleep restoration test.
No cold reboot or long-term session validation was performed. Unknown OpenXR apps
remain unchanged until a reviewed profile exists.

Update both suites while disabled before enabling either: the old Steam Link Suite
must restore its HAL gate before the new Controller Suite can own it. Parent-suite
supervision and the existing KernelSU installation/recovery contracts remain.
Old migration backups were archived locally with matching SHA256 before removing
those exact backups and this installation's temporary files from the headset.
Evidence: ignored dist/deploy-0.1.2-20261018/ (active status, priorities and backup).

Published baseline: KernelSU GXR 1.0.11 (32677), both suites 0.1.1. GitHub build,
feed and catalogue checks passed; the Manager manifest matches the retained signing
certificate and release APK digest. Seven constituent modules are deprecated and
removed from modules.json; six active catalogue entries remain. Historical releases
and retirement feeds remain intact. Local artifact hashes are in the ignored
`dist/publication-and-split-20261018.json` receipt.
