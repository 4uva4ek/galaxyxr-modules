# Galaxy XR Modules

Independent KernelSU modules for Samsung Galaxy XR in one repository.
Each module has its own version and ZIP package. Build tools and device safety
rules are shared across the repository.

| Module | Purpose | Version | Release |
| --- | --- | --- | --- |
| [Haptics MAIN](modules/haptic-main/README.md) | MAIN motor, step 2 frequency, unchanged amplitude | 0.5.1 | [Regular](https://github.com/4uva4ek/galaxyxr-modules/releases/tag/haptic-main/v0.5.1) |
| [Velocity Frame](modules/openxr-velocity-frame/README.md) | Correct velocity coordinates before app-side prediction | 0.3.0 | [Regular](https://github.com/4uva4ek/galaxyxr-modules/releases/tag/openxr-velocity-frame/v0.3.0) |
| [Controller Extrapolation](modules/controller-extrapolation/README.md) | Enable OpenXR prediction in Steam Link | 0.4.0 | [Regular](https://github.com/4uva4ek/galaxyxr-modules/releases/tag/controller-extrapolation/v0.4.0) |

All new packages are automatic variants, shipped disabled. Enable separately after
installation. Velocity Frame 0.3.0 and Extrapolation 0.4.0 support each other;
upgrade both, stop the app, and clear old live patches first. Combined tracking quality and prediction overshoot still
require testing. Controller Extrapolation 0.4.0 is a regular release following
user confirmation of good operation on 2026-10-11.

**Action:** reports state and recovery commands; it does not toggle the patch.
Apply through the
standard KernelSU `service.sh` stage and disable auto on a detected failure.
SHA-256 files are attached beside each automatic ZIP in its release.

**None of the modules in this repository modify the contents of system partitions.**
These modules apply their changes only to private process memory; original APKs
and libraries on disk remain unchanged. They do not flash partitions, remount
them for writing, or replace system files through mounts. A full reboot clears
the in-memory changes. With opt-in autostart enabled, KernelSU can reapply them;
disable auto before rebooting to restore stock permanently.

Module files and optional autostart settings remain under `/data/adb/modules/`; runtime logs and locks are stored
in `/dev` (tmpfs). This statement covers the modules themselves. The tested
Samsung KernelSU installer can separately rewrite startup files under `/metadata/`;
follow the manual deployment instructions linked below.

Each module ships an `-auto.zip` package. It uses standard KernelSU startup and disables itself on a detected failure. See [activation and recovery](docs/AUTOSTART.md).

## Installation instructions

- [Install KernelSU GXR with DFRoot](https://github.com/4uva4ek/KernelSU-GXR/blob/galaxyxr/GALAXYXR.md#installation).
- [Haptic MAIN module installation](modules/haptic-main/README.md#installation), including the tested installer's persistent-write caveat.
- [Controller extrapolation deployment](modules/controller-extrapolation/README.md#build-and-deployment) (manual deployment only).
- [OpenXR velocity correction](modules/openxr-velocity-frame/README.md#installation-and-rollback): automatic activation, third-party app discovery, exact firmware checks. Use Extrapolation 0.4.0 or newer; earlier combinations are unsupported.

Read the linked instructions and safety notes before installing.

## Repository layout

```text
modules/haptic-main/    source, module.prop, validation, and instructions
modules/controller-extrapolation/  OpenXR extrapolation switcher
modules/openxr-velocity-frame/     runtime velocity correction
tools/                 shared build tools
docs/                  release guidelines and validation records
dist/                  local build artifacts, excluded from Git
```

## Building

Requires Python 3.10+ and Android NDK **28.2.13676358**. The ARM64 integration test additionally requires `unicorn==2.1.4`. Set `ANDROID_NDK_HOME` to your NDK installation,
or pass its path with `--ndk`.

```sh
python tools/build.py haptic-main --ndk /path/to/android-ndk
python modules/haptic-main/tests/verify_package.py
python modules/openxr-velocity-frame/build_module.py --ndk /path/to/android-ndk --runtime /path/to/libopenxr_android.so
python tools/build.py controller-extrapolation --ndk /path/to/android-ndk
python modules/controller-extrapolation/tests/verify_package.py
```

Output: `dist/haptic-main/gxr-haptic-main-0.5.1-auto.zip` and a SHA-256 checksum file.
Extrapolation output: `dist/controller-extrapolation/gxr-controller-extrapolation-0.3.1-auto.zip`
and a SHA-256 checksum file for each ZIP. Only automatic release packages are built.
The build does not connect to the headset or install the module.

## Project rules

Keeping the headset bootable takes priority over experimental features.
Modifications must preserve original system files and have a clear rollback
path. The automatic package
uses the failure guards described in [AUTOSTART.md](docs/AUTOSTART.md).
See [AGENTS.md](AGENTS.md) for the complete requirements.

See [Action reports](docs/ACTION-REPORT.md) and [checkbox limitations](docs/LIVE-CONTROL.md).
Immediate checkbox activation requires [KernelSU GXR live-control](https://github.com/4uva4ek/KernelSU-GXR/releases).
Modules without the opt-in flag retain standard behavior; Action remains a status report.

Samsung and Steam Link APKs, device dumps, signing keys, dependencies, and compiled
files are excluded from Git. Module ZIPs are published separately in Releases.
Versions and tags are independent for each module: `haptic-main/v0.4.1`
and `controller-extrapolation/v0.3.1`.
See the [release guidelines](docs/RELEASING.md) and
[original headset validation record](docs/VALIDATION-2026-10-10.md) and
[historical variant validation record](docs/VALIDATION-2026-10-11-AUTOSTART.md).
