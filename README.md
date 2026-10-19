# Galaxy XR KernelSU Modules

KernelSU modules for the Samsung Galaxy XR (SM-I610) running Android XR. Tracking and haptic modules apply changes in process memory at runtime. OTA Control changes persistent Android package states and an update setting. No module modifies original system APKs or libraries or flashes partitions. Every package installs **disabled**; you enable it yourself.
Requires a rooted Samsung Galaxy XR with [KernelSU-GXR](https://github.com/4uva4ek/KernelSU-GXR) (1.0.6 or later for Sources installation).

Add this repository as a source within the KernelSU Manager with the url: ``https://github.com/4uva4ek/galaxyxr-modules``.
Open **Modules → Sources**, select the modules you want and choose **Install selected**. Each module has its own version and update feed, so the Manager offers updates per module. Automatic ZIPs are also attached to [GitHub Releases](https://github.com/4uva4ek/galaxyxr-modules/releases); see [installing from Sources or ZIP](docs/INSTALLING-MODULES.md).

For memory patches after installation: tick the module's checkbox, then use the Manager's documented soft reboot. The **Action** button only reports live status and recovery steps — it never toggles a patch. Untick the checkbox to restore the original code; a full reboot clears every memory patch, but an enabled module applies again at the next start. OTA Control has separate restore instructions below; reboot does not undo its persistent changes.

## Modules

### Haptic MAIN (`gxr_haptic_main`)
Routes standard controller haptics to the **grip (MAIN) motor** instead of the trigger motor, with retuned pulse timing. Works with any app.

### Controller Suite (`gxr_controller_suite`)
Combines Whitening, Timestamp Precision, HAL Cache Bypass, tail-velocity
correction and HAL-wide LogLevel=0 control. One activation preference and English/Russian WebUI.
[Components and migration](modules/controller-suite/README.md).

### Steam Link Suite (`gxr_steamlink_suite`) - deprecated
> **Deprecated 2026-10-19, no successor.** Removed from Sources; 0.1.7 is the final
> release. Disable it, then remove it in the Manager.

Combines Velocity Frame, Pose Time, OpenXR app priority profiles and optional
Controller Extrapolation. The initial priority profile targets selected Steam Link
threads at nice=-20 while the app is visible and the headset is awake. Other apps
remain unchanged until a profile is added. Pose-time compensation defaults to 0;
extrapolation defaults OFF.
[Components and migration](modules/steamlink-suite/README.md).

Update **KernelSU GXR to 1.0.11 or later** before installing these suites.
The Manager asks to restore/remove replaced standalone modules; the suites install
disabled. Enable the desired suites, then use the Manager soft restart. Haptic MAIN
remains separate. Replaced standalone modules are deprecated and removed from Sources; their
existing update feeds retain retirement notices.

**Updating to 0.1.2:** disable both suites, update both, then enable them and use
the Manager soft restart. Update both before enabling either: HAL LogLevel=0
ownership moved from Steam Link Suite to Controller Suite. Do not enable a mixed
0.1.1/0.1.2 pair. Startup and active Steam Link checks passed on a headset;
see [validation and limits](docs/SUITES-0.1.2-VALIDATION.md).

### OpenXR QUALITY (`gxr_openxr_quality`)
Holds the camera tracking **QUALITY** request during visible OpenXR sessions.
Experimental: sleep/app exit releases it; critical thermal severity 4 releases
it until severity falls to 2. This is not a guarantee of camera frame rate or
better controller motion. [Behavior, validation limits and local 0.1.0 migration](modules/openxr-quality/README.md).

### OTA Control (`gxr_ota_control`)
Blocks Samsung firmware updates by disabling `com.wssyncmldm` and
`com.sec.android.soagent` for user 0 and setting `ota_disable_automatic_update=1`.
Saves the original state for restoration. These changes **survive reboot**;
unticking the checkbox only stops future reapplication. Run the module's
`action.sh off` as root to restore the saved baseline. Installs disabled;
host-tested, **not yet validated on a headset**.
[Controls, recovery and validation limits](modules/ota-control/README.md).

### MV-HEVC Eye Fix (`gxr_mvhevc_eye_fix`)
Corrects the **left/right eye inversion** Samsung Gallery XR shows for MV-HEVC
spatial videos. The patched `libstagefright.so` and `libaplayer.so` are generated
on the device from the installed stock libraries — hash-checked before and after
patching — and bind-mounted over them; no vendor binaries ship in the package.
Left-primary MV-HEVC video only. [Patches and validation](modules/mvhevc-eye-fix/README.md).

### Deprecated and retired
**Framebuffer10** (10-bit SDR output with corrected camera colors) is deprecated and source-only — its payload contains vendor code and is not distributed. Earlier tracking experiments (Bracket, IMU Time, HAL 360, native copy) are retired and kept locally; they are not distributed here.

## Safety

- Packages install **disabled**; installation never enables a module.
- Tracking and haptic patches live only in process RAM and check exact firmware/library compatibility before activation. OTA Control changes persistent Android state and restores a saved baseline.
- Original APKs and libraries are preserved; modules do not flash partitions or change root setup.
- Failures disable the module and record the cause for **Action** to show. Follow the module's recovery instructions if restoration is incomplete.
- Disable memory-patch modules before relying on reboot as rollback — an enabled module reapplies. For OTA Control, use `action.sh off` to restore; reboot alone does not restore updates.

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

Raising both `version` and `versionCode` in a public module's `module.prop` and
pushing to `main` builds, checks and publishes its disabled ZIP automatically for
modules supported by the CI builder. The two suites currently require their
dedicated local builders and reviewed release uploads. See [automatic releases](docs/AUTOMATIC-RELEASES.md)
for build-only runs, private inputs, validation requirements and recovery.

Python 3.10+ and Android NDK **28.2.13676358**. Build commands never connect to the headset.

```sh
python tools/build.py haptic-main --ndk /path/to/android-ndk
# App modules need the compatibility contract generated from your own pinned runtime first:
python modules/openxr-velocity-frame/prepare.py --ndk /path/to/android-ndk --runtime /path/to/libopenxr_android.so
python tools/build.py controller-extrapolation --ndk /path/to/android-ndk
```

Only `-auto.zip` packages and `.zip.sha256` files are produced under `dist/MODULE/`. Modules with dedicated build commands document them in their READMEs. Proprietary libraries, APKs, device dumps and compiled artifacts are never committed. Release rules: [RELEASING.md](docs/RELEASING.md), source layout: [MODULE-LAYOUT.md](docs/MODULE-LAYOUT.md).

Author: **4uva4ek**.
