# Live control release validation

Haptic MAIN 0.4.1, Controller Extrapolation 0.3.1, OpenXR Velocity Frame 0.2.1.
Names contain Live control and module.prop opts in with gxrLiveControl=1.
KernelSU GXR 1.1.0 carries the userspace hook; Action remains unchanged.

Both variants were built, hashes and archive contents verified. Manual reports
were tested before auto deployment; haptic manual on/off succeeded. Existing
OpenXR correction had previously passed on/off with a live app; this session's
app target was absent, so its automatic worker was verified waiting.

The installed Manager's own libksud.so was used for haptic off/on, confirming
native stock/patched states and checkbox marker changes. Its 30-second health
check cleared pending. An inert fixture tested failure (exit 7), disabled state,
explicit recovery, and cleanup. Root stayed available after APK replacement.
The same certificate, v2-only APK signature, unchanged Manager DEX and packaged
ksud were verified. No kernel, system-library or startup metadata write occurred.

The English Action report presents ENABLED/DISABLED and separate waiting/error
reasons, independent of the native patch's technical name. All module checksums
were verified on installation. No direct UI tap test, full reboot, or new
subjective haptic/throw validation was performed. Background state changes may
require refreshing the Manager list. Non-opt-in standard paths were reviewed in
source; they were not executed on device because of existing metadata behavior.
