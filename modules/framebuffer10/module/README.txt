Galaxy XR Framebuffer10 0.5.0-experimental (author 4uva4ek)

Continuous 10-bit SDR output with correct colours on firmware I610UEU2AZF3.
Removes 8-bit banding in gradients. Not HDR: brightness and gamut unchanged.
Requires KernelSU GXR 1.0.6 or later (coordinated restart, generic installer).

No vendor code ships in this package. Before the first activation the module
generates its color library from this headset's own stock compositor library
(stock and result SHA-256 verified) and keeps it in the module folder.
Any other firmware is refused and the module stays disabled.

Installs disabled. Check the module, then use a KernelSU soft reboot.
Uncheck to turn it off; it restores stock 8-bit output. Immediate restore:
sh /data/adb/modules/gxr_framebuffer10/action.sh off
Original system files and partitions are never written; a temporary
read-only bind is used during startup only. Full power removal clears all
runtime changes. Action reports the verified state and recovery steps.
