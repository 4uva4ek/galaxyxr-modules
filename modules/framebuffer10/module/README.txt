Galaxy XR Framebuffer10 0.3.1-experimental

Continuous 10-bit SDR with corrected camera shader output. Not full HDR.
Requires the KernelSU GXR coordinated restart API; 1.0.3 adds hook diagnostics.
Prepare temporary startup changes while Android is stopped, start once through
KernelSU, verify output, then remove the bind and selector. An independent
guardian monitors the running session. Original system files are not modified.

Install/update disabled. Enable separately, then request a coordinated restart.
Action reports actual status and recovery instructions. Disable requests verified
restoration, which may restart Android. Full power removal clears runtime changes;
enabled modules can reapply at a later startup. Cold kernel boot expires old
failure guards without changing ON/OFF preferences; soft reboot retains guards.

Three consecutive soft restarts passed. The prior started-hook exit 1 did not
recur, but its original cause is unknown. Cold-boot/visual validation of the final
combination remains pending. Separate-draw UNORM alpha blending remains a limit.

This local ZIP contains a modified proprietary Samsung compositor library and
must not be publicly redistributed. Only project-authored source is published.
See NATIVE-NOTICE.txt in the source tree (NOTICE.txt in the local ZIP), README.md
and docs/COORDINATED-RESTART-2026-10-11.md for requirements and protocol details.
