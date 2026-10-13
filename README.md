# Galaxy XR KernelSU Modules

KernelSU modules for the Samsung Galaxy XR (SM-I610) running Android XR. All modules are non-invasive — they patch private process memory at runtime and never modify system files, partitions or the original APKs. Every package installs **disabled**; you enable it yourself.
Requires a rooted Samsung Galaxy XR with [KernelSU-GXR](https://github.com/4uva4ek/KernelSU-GXR) (1.0.6 or later for Sources installation).

Add this repository as a source within the KernelSU Manager with the url: ``https://github.com/4uva4ek/galaxyxr-modules``.
Open **Modules → Sources**, select the modules you want and choose **Install selected**. Each module has its own version and update feed, so the Manager offers updates per module. Automatic ZIPs are also attached to [GitHub Releases](https://github.com/4uva4ek/galaxyxr-modules/releases); see [installing from Sources or ZIP](docs/INSTALLING-MODULES.md).

After installation: tick the module's checkbox, then use the Manager's documented soft reboot. The **Action** button only reports live status and recovery steps — it never toggles a patch. Untick the checkbox to restore the original code; a full reboot clears every memory patch, but an enabled module applies again at the next start.

## Modules

### Haptic MAIN (`gxr_haptic_main`)
Routes standard controller haptics to the **grip (MAIN) motor** instead of the trigger motor, with retuned pulse timing. Works with any app.

### OpenXR Velocity Frame (`gxr_openxr_velocity_frame`)
Corrects controller **linear and angular velocity coordinates** handed to third-party OpenXR apps such as Steam Link. Fixes wrong throw directions; no added prediction or smoothing.

### Steam Link Pose Time (`gxr_steamlink_pose_time`)
Forwards the **real timestamp of each controller pose** to the PC inside Steam Link's packets, so SteamVR stops guessing when the pose was sampled. Steadier PCVR controller motion. Requires Steam Link 2.0.20 and Velocity Frame.

### Controller Extrapolation (`gxr_controller_extrapolation`)
Enables the stock Android XR **consumer-side controller extrapolation** inside the Steam Link process (a runtime flag Samsung ships disabled). Experimental; not for use together with Pose Time.

### Controller Whitening (`gxr_controller_whitening`)
Corrects a **process-covariance whitening** defect in Samsung's controller tracking service. Subtle; affects all apps.

### Controller Timestamp Precision (`gxr_controller_timestamp_precision`)
Keeps controller pose **request timestamps at integer precision** inside the controller HAL instead of a lossy float conversion. Affects all apps.

### HAL Cache Bypass (`gxr_hal_cache_bypass`)
Makes the controller HAL **recompute each requested pose** instead of returning its predictor cache. Experimental; the stock predictor's other filters remain.

### Deprecated and retired
**Framebuffer10** (10-bit SDR output with corrected camera colors) is deprecated and source-only — its payload contains vendor code and is not distributed. Earlier tracking experiments (Bracket, IMU Time, HAL 360, native copy) are retired and kept locally; they are not distributed here.

## Safety

- Packages install **disabled**; installation never enables a module.
- Patches live only in process RAM. Original APKs, libraries, partitions and root setup are never written.
- Every module checks exact firmware and library hashes before touching anything and refuses unknown builds.
- Failures are contained: a failed activation disables the module, restores the original code and records the cause for **Action** to show. Guards reset after a confirmed cold boot.
- Disable a module before relying on reboot as rollback — an enabled module reapplies.

Details: [startup and recovery](docs/AUTOSTART.md), [Action reports](docs/ACTION-REPORT.md), [cold-boot guards](docs/COLD-BOOT-GUARDS-2026-10-11.md), [update feeds and release process](docs/UPDATES.md).

## Repository layout

```text
modules/             Module sources, lifecycle scripts and build definitions
modules.json         Public module catalogue for the Manager
updates/             Published versions, ZIP URLs and SHA-256 hashes
changelogs/          Per-module release notes
tools/               Shared build, validation and release tools
docs/                Installation, deployment, compatibility and validation records
```

## Building

Python 3.10+ and Android NDK **28.2.13676358**. Build commands never connect to the headset.

```sh
python tools/build.py haptic-main --ndk /path/to/android-ndk
# App modules need the compatibility contract generated from your own pinned runtime first:
python modules/openxr-velocity-frame/prepare.py --ndk /path/to/android-ndk --runtime /path/to/libopenxr_android.so
python tools/build.py controller-extrapolation --ndk /path/to/android-ndk
```

Only `-auto.zip` packages and `.zip.sha256` files are produced under `dist/MODULE/`. Modules with dedicated build commands document them in their READMEs. Proprietary libraries, APKs, device dumps and compiled artifacts are never committed. Release rules: [RELEASING.md](docs/RELEASING.md), source layout: [MODULE-LAYOUT.md](docs/MODULE-LAYOUT.md), CI builds: [CI.md](docs/CI.md).

Author: **4uva4ek**.
