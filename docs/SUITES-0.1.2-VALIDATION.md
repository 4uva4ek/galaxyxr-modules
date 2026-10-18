# Suite 0.1.2 local candidate - 2026-10-18

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

ADB had no connected devices. These candidates are not installed, activated,
device-tested or published. No controller smoothness/latency improvement is claimed.
Update both suites while disabled before enabling either: the old Steam Link Suite
must restore its HAL gate before the new Controller Suite can own it. Parent-suite
supervision and the existing KernelSU installation/recovery contracts remain.

Published baseline: KernelSU GXR 1.0.11 (32677), both suites 0.1.1. GitHub build,
feed and catalogue checks passed; the Manager manifest matches the retained signing
certificate and release APK digest. Seven constituent modules are deprecated and
removed from modules.json; six active catalogue entries remain. Historical releases
and retirement feeds remain intact. Local artifact hashes are in the ignored
`dist/publication-and-split-20261018.json` receipt.
