# HAL Prediction +30 ms 0.1.1 ? Experimental

Adds 30,000,000 ns to the shared Samsung controller service's HAL pose request time, for both controllers. It does not directly change acceleration or velocity and does not add v*dt pose extrapolation. Descriptor/output timestamp handling is unchanged.

The native payload is the previously manually tested 16-byte patch at service library offset 0x17758 with an RX tail at 0x21210. The exact library SHA256 and entire supported live RX range are checked. All threads are stopped for mutation and released afterwards. Off restores the branch while retaining the inert tail for in-flight safety. No original library is overwritten.

Compatibility (0.1.1): version-independent ownership of the haptic JNI function
`nativePerformHapticFeedback` at `[0x122ac,0x12914)`. Prediction ignores all live
instruction differences within that function; Haptic validates its own implementation.
No Haptic version, signature or package-presence check is used. This deliberately
also tolerates another writer inside that range; the guard cannot authenticate its
origin. The pinned on-disk firmware hash, all RX bytes outside this range, and the
Prediction hook/payload remain checked. Prediction never writes/restores Haptic's
range. Future Haptic changes contained there require no Prediction rebuild;
changes outside it require an ownership-contract update, not a version allowlist.


The user explicitly requested the automatic variant, installed disabled, on 2026-10-11 and reaffirmed this after being told that it targets a shared system service. This is a narrow exception to the prior no-system-autostart preference, not permission to modify firmware, partitions, root startup, other services or other modules.

Build only the automatic ZIP, containing a disable marker, per the latest user instruction. Historical manual archives are not rebuilt. The automatic ZIP waits for Android boot completion, patches one stable controller-service identity, checks process identity every two seconds and code state every 30 seconds, and disables on detected errors or service restart without reapplying. An interrupted initial activation/30-second observation leaves a persistent marker preventing unattended retry. Those checks do not prove actual tracking correctness or guarantee detection of a hung service.

KernelSU's checkbox is the sole saved activation preference. gxrLiveControl=1 dispatches start/restore with the project's existing KernelSU GXR userspace. Action only reports English runtime status and recovery instructions. No KernelSU modifications are part of this module.

Recovery: uncheck the module, verify DISABLED using Action. If restoration cannot be verified, leave it disabled and reboot. A still-enabled automatic module can reapply after reboot; reboot alone is not rollback. No full reboot or automatic enabled live test is claimed. Prior manual on/off and user native tracking feedback are historical evidence, not validation of automatic lifecycle.

Deployment must extract into a new staging directory, verify SHA256SUMS, and publish the directory with disable already present to /data/adb/modules/gxr_hal_prediction_30. Never use the generic module installer on this headset: it can rewrite metadata startup files. Do not execute enable/on during disabled deployment.

This is an experimental prerelease. It can coexist with Velocity Frame 0.3.0 and
Controller Extrapolation 0.4.0 at the code level, but the combined prediction
horizon and motion quality have not been user-validated.

Build: `python modules/hal-prediction-30/build_module.py --ndk /path/to/ndk`.
The Windows native/lifecycle regression harness uses `ANDROID_NDK_HOME` and
`GXR_WORKSPACE` pointing to a local evidence workspace containing the recorded
`diagnostics/hal-prediction-no-shizuku-20261011` fixtures. Proprietary runtime
captures are not distributed. The module build itself does not require them.
