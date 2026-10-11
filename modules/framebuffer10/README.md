# Framebuffer10 0.3.1 (experimental, source-only)

Enables continuous 10-bit SDR output with corrected camera shader encoding on
the pinned Galaxy XR I610UEU2AZF3 firmware. This does not enable full HDR.

Requires KernelSU GXR's coordinated restart API, introduced in 1.0.2; use 1.0.3
for hook diagnostics. The module prepares a temporary compositor-library bind
and startup overrides while Android is stopped. KernelSU starts Android once;
after verification the module removes the bind and overrides. Original files
remain unchanged. A guardian monitors the running mapping and output.

Three consecutive soft restarts passed. The original 0.3.0 started-hook exit 1
was not reproduced and its exact cause remains unknown. Full cold-boot validation
of the final combination and visual quality remain unverified.

## Local build only

The ZIP requires a separately obtained, modified proprietary compositor library
at `module/payload/libvulkan_compositor.so`, matching the hash in `build.json`.
That payload is ignored by Git and is not supplied here. An arbitrary stock
library is not a substitute. This repository does not currently contain a
standalone recipe for producing that shader payload from stock firmware.

With the exact payload already available, build locally using:

```sh
python tools/build.py framebuffer10 --ndk /path/to/android-ndk
python modules/framebuffer10/tests/test_lifecycle.py
```

Do not redistribute the resulting vendor-containing ZIP. See
[NATIVE-NOTICE.txt](NATIVE-NOTICE.txt). Only project-authored source is published.

Install/update disabled, restoring an existing session before replacement.
Enable separately, then request a KernelSU coordinated soft reboot. Action
reports status and recovery instructions. Disabling an active session can
restart Android to restore the original display. Full power removal clears
runtime changes; enabled modules can reapply at a later authorized startup.

[Lifecycle, rollback and logs](../../docs/COORDINATED-RESTART-2026-10-11.md) |
[Cold-boot guards](../../docs/COLD-BOOT-GUARDS-2026-10-11.md)
