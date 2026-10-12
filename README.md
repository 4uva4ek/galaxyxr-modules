# Galaxy XR Modules

Independent KernelSU modules for Samsung Galaxy XR. Each has its own version.

Stable KernelSU update feeds live in [`updates/`](updates/); see the
[update and release workflow](docs/UPDATES.md). The update-aware GXR Manager can
check OFF modules and install reviewed replacements disabled. Older installers
remain blocked by the packages' safety checks.

Add `https://github.com/4uva4ek/galaxyxr-modules` under **Sources** in the
update-aware Manager, select modules, and choose **Install selected**.
The [root catalogue](modules.json) is the single public module registry.

```text
modules/             Module sources, lifecycle scripts and build definitions
modules.json         Public module catalogue for the Manager
updates/             Published versions, ZIP URLs and SHA-256 hashes
changelogs/          Per-module release notes
tools/               Shared build, validation and release tools
.github/workflows/   Catalogue checks and stable feed publication
docs/                Deployment, compatibility and validation records
```

Compiled automatic ZIPs live in GitHub Releases, not in the source tree.
Public automatic ZIPs ship disabled; Framebuffer10 is deprecated and source-only. Build tools and device safety rules are shared.

Framebuffer10 0.3.1 uses KernelSU GXR 1.0.3's
[coordinated restart stages](docs/COORDINATED-RESTART-2026-10-11.md) to prepare
buffers before Android starts and avoid a second successful-path restart.
Three soft restarts were verified locally; details and remaining limits are in that report.

Current modules expire interruption guards at the first startup after a
confirmed cold kernel boot, while preserving both ON and OFF checkbox states.
Soft reboot retains guards. See [cold-boot guards and validation](docs/COLD-BOOT-GUARDS-2026-10-11.md).
See the [release changes and validation limits](docs/RELEASE-2026-10-11.md).
The table below lists public packages.

| Module | Purpose | Version | Automatic package |
| --- | --- | --- | --- |
| [Haptic MAIN](modules/haptic-main/README.md) | Route standard haptics to the grip motor | 0.5.6 | [ZIP](https://github.com/4uva4ek/galaxyxr-modules/releases/download/haptic-main/v0.5.6/gxr-haptic-main-0.5.6-auto.zip) |
| [Controller Extrapolation](modules/controller-extrapolation/README.md) | Enable stock OpenXR extrapolation in Steam Link memory | 0.4.3 | [ZIP](https://github.com/4uva4ek/galaxyxr-modules/releases/download/controller-extrapolation/v0.4.3/gxr-controller-extrapolation-0.4.3-auto.zip) |
| [OpenXR Velocity Frame](modules/openxr-velocity-frame/README.md) | Correct controller velocity coordinates in eligible third-party apps | 0.3.5 | [ZIP](https://github.com/4uva4ek/galaxyxr-modules/releases/download/openxr-velocity-frame/v0.3.5/gxr-openxr-velocity-frame-0.3.5-auto.zip) |
| [Controller Whitening](modules/controller-whitening/README.md) | Correct process covariance whitening in the controller service | 0.1.4 | [ZIP](https://github.com/4uva4ek/galaxyxr-modules/releases/download/controller-whitening/v0.1.4/gxr-controller-whitening-0.1.4-auto.zip) |
| [Controller Timestamp Precision](modules/controller-timestamp-precision/README.md) | Preserve integer request timestamps in the controller HAL | 0.1.3 | [ZIP](https://github.com/4uva4ek/galaxyxr-modules/releases/download/controller-timestamp-precision/v0.1.3/gxr-controller-timestamp-precision-0.1.3-auto.zip) |
| [HAL Cache Bypass](modules/hal-cache-bypass/README.md) | Recompute controller poses instead of reusing the normal HAL predictor cache | 0.1.1 | [ZIP](https://github.com/4uva4ek/galaxyxr-modules/releases/download/hal-cache-bypass/v0.1.1/gxr-hal-cache-bypass-0.1.1-auto.zip) |

[Steam Link Pose Time 0.1.2](modules/steamlink-pose-time/README.md) forwards the
full controller slot timestamp to PCVR. Requires Steam Link 2.0.20 and Velocity
Frame; this is not a universal OpenXR timing fix.
[Download automatic ZIP](https://github.com/4uva4ek/galaxyxr-modules/releases/download/steamlink-pose-time/v0.1.2/gxr-steamlink-pose-time-0.1.2-auto.zip).
See its documented validation limits before enabling.

[Framebuffer10 0.3.1 (deprecated) source and local-build requirements](modules/framebuffer10/README.md)
are also available. Its proprietary compositor payload and local ZIP are not distributed.

SHA-256 files accompany release ZIPs. Retired controller-velocity-frame and
openxr-prediction-40 experiments are retained locally and are not distributed here.

Action reports runtime state and recovery commands. It never toggles a patch.
The KernelSU checkbox saves the startup preference. Enabled modules start through
the standard `service.sh` stage; in the documented GXR 1.0 temporary-root flow,
this follows the soft reboot after root acquisition. Checking an idle module does
not start it immediately. See [startup and recovery](docs/AUTOSTART.md) and
[Action reports](docs/ACTION-REPORT.md).

The seven catalogue modules modify private process memory. Framebuffer10 uses
a temporary library bind and startup overrides, removes them after verification,
and retains the live compositor mapping. Original APKs and libraries remain
unchanged; no module flashes partitions or changes root acquisition. Full reboot clears memory patches, but enabled modules can
apply again at a later service stage. Disable before using reboot as rollback.
Persistent module preferences and failure records live under `/data/adb/modules/`;
runtime logs and locks use `/dev` tmpfs.

## Installation

Follow [KernelSU GXR with DFRoot](https://github.com/4uva4ek/KernelSU-GXR/blob/galaxyxr/GALAXYXR.md#installation)
for root setup, then [install from Sources or ZIP](docs/INSTALLING-MODULES.md).
Use a GXR Manager with the generic GXR lifecycle-contract installer for all seven modules.
Older versions with the five-module allowlist cannot install HAL Cache Bypass.
Older/standard Samsung KernelSU installers can rewrite startup metadata; these
ZIPs still block that path. The module READMEs retain reviewed direct-deployment
instructions for installations without the updated Manager.

Install or update disabled. Disable and restore an existing module before replacing
its directory. Activation requires a separate decision after verification; an
installation or positive test does not enable a replacement automatically.

## Building

Python 3.10+ and Android NDK **28.2.13676358** are required. Build commands never
connect to the headset or install a module.

```sh
python tools/build.py haptic-main --ndk /path/to/android-ndk
python modules/haptic-main/tests/verify_package.py
# Generate the shared compatibility contract from your own pinned runtime first:
python modules/openxr-velocity-frame/prepare.py --ndk /path/to/android-ndk --runtime /path/to/libopenxr_android.so
python tools/build.py controller-extrapolation --ndk /path/to/android-ndk
python modules/controller-extrapolation/tests/verify_package.py
```

Only `-auto.zip` packages and `.zip.sha256` files are produced under `dist/MODULE/`.
OpenXR Velocity Frame and the controller service modules have dedicated build and
validation commands in their READMEs. Proprietary libraries, APKs, device dumps,
keys and compiled artifacts are excluded from Git.
The generated `compatibility.generated.h` is also excluded: both app modules use
the exact contract generated from your runtime by the preparation step above.

## Project rules

Keeping the headset bootable takes priority. Preserve original files, precise
compatibility guards, failure quarantine and verified restoration. See
the [release guidelines](docs/RELEASING.md) and the module
validation records. Historical records describe their original versions, not the
current deployment state or publishing policy.
