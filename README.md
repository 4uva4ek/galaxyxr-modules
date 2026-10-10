# Galaxy XR Modules

Independent KernelSU modules for Samsung Galaxy XR in one repository.
Each module has its own version and ZIP package. Build tools and device safety
rules are shared across the repository.

| Module | Purpose | Version | ZIP |
| --- | --- | --- | --- |
| [haptic-main](modules/haptic-main/README.md) | Route SUB haptics to MAIN in service memory | 0.4.1 | [Download auto](https://github.com/4uva4ek/galaxyxr-modules/releases/download/haptic-main/v0.4.1/gxr-haptic-main-0.4.1-auto.zip) |
| [controller-extrapolation](modules/controller-extrapolation/README.md) | Enable stock OpenXR controller extrapolation in Steam Link memory | 0.3.1 | [Download auto](https://github.com/4uva4ek/galaxyxr-modules/releases/download/controller-extrapolation/v0.3.1/gxr-controller-extrapolation-0.3.1-auto.zip) |
| [openxr-velocity-frame](modules/openxr-velocity-frame/README.md) | Correct controller velocity coordinates in third-party apps using the supported OpenXR runtime | 0.2.1 | [Download auto](https://github.com/4uva4ek/galaxyxr-modules/releases/download/openxr-velocity-frame/v0.2.1/gxr-openxr-velocity-frame-0.2.1-auto.zip) |

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
- [OpenXR velocity correction](modules/openxr-velocity-frame/README.md#installation-and-rollback): automatic activation, third-party app discovery, exact firmware checks. Keep the separate extrapolation patch off with this release.

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

Requires Python 3.10+ and Android NDK **28.2.13676358**. No third-party Python
or npm packages are required. Set `ANDROID_NDK_HOME` to your NDK installation,
or pass its path with `--ndk`.

```sh
python tools/build.py haptic-main --ndk /path/to/android-ndk
python modules/haptic-main/tests/verify_package.py
python tools/build.py controller-extrapolation --ndk /path/to/android-ndk
python modules/controller-extrapolation/tests/verify_package.py
```

Output: `dist/haptic-main/gxr-haptic-main-0.4.1-auto.zip` and a SHA-256 checksum file.
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
