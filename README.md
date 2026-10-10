# Galaxy XR Modules

Independent KernelSU modules for Samsung Galaxy XR in one repository.
Each module has its own version and ZIP package. Build tools and device safety
rules are shared across the repository.

| Module | Purpose | Version | Manual ZIP | Auto ZIP |
| --- | --- | --- | --- | --- |
| [haptic-main](modules/haptic-main/README.md) | Route SUB haptics to MAIN in service memory | 0.3.0 | [Download manual](https://github.com/4uva4ek/galaxyxr-modules/releases/download/haptic-main/v0.3.0/gxr-haptic-main-0.3.0-manual.zip) | [Download auto](https://github.com/4uva4ek/galaxyxr-modules/releases/download/haptic-main/v0.3.0/gxr-haptic-main-0.3.0-auto.zip) |
| [controller-extrapolation](modules/controller-extrapolation/README.md) | Enable stock OpenXR controller extrapolation in Steam Link memory | 0.2.0 | [Download manual](https://github.com/4uva4ek/galaxyxr-modules/releases/download/controller-extrapolation/v0.2.0/gxr-controller-extrapolation-0.2.0-manual.zip) | [Download auto](https://github.com/4uva4ek/galaxyxr-modules/releases/download/controller-extrapolation/v0.2.0/gxr-controller-extrapolation-0.2.0-auto.zip) |
| [openxr-velocity-frame](modules/openxr-velocity-frame/README.md) | Correct controller velocity coordinates in third-party apps using the supported OpenXR runtime | 0.1.0 | [Download manual](https://github.com/4uva4ek/galaxyxr-modules/releases/download/openxr-velocity-frame/v0.1.0/gxr-openxr-velocity-frame-0.1.0-manual.zip) | [Download auto](https://github.com/4uva4ek/galaxyxr-modules/releases/download/openxr-velocity-frame/v0.1.0/gxr-openxr-velocity-frame-0.1.0-auto.zip) |

**Manual:** enable with KernelSU's Action button. **Auto:** apply through the
standard KernelSU `service.sh` stage and disable auto on a detected failure.
Install one variant per module; they share the same module ID. SHA-256 files are
attached beside both ZIPs in each release.

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

Each module has separate `-manual.zip` and `-auto.zip` packages. The automatic
variant uses standard KernelSU startup and disables itself on a detected failure. See [mode selection and recovery](docs/AUTOSTART.md).

## Installation instructions

- [Install KernelSU GXR with DFRoot](https://github.com/4uva4ek/KernelSU-GXR/blob/galaxyxr/GALAXYXR.md#installation).
- [Haptic MAIN module installation](modules/haptic-main/README.md#installation), including the tested installer's persistent-write caveat.
- [Controller extrapolation deployment](modules/controller-extrapolation/README.md#build-and-deployment) (manual deployment only).
- [OpenXR velocity correction](modules/openxr-velocity-frame/README.md#installation-and-rollback): both variants, third-party app discovery, exact firmware checks. Keep the separate extrapolation patch off with this release.

Read the linked instructions and safety notes before installing.

## Repository layout

```text
modules/haptic-main/    source, module.prop, validation, and instructions
modules/controller-extrapolation/  OpenXR extrapolation switcher
modules/openxr-velocity-frame/     runtime velocity correction, manual and auto
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

Output: `dist/haptic-main/gxr-haptic-main-0.3.0-{manual,auto}.zip` and a SHA-256 checksum file.
Extrapolation output: `dist/controller-extrapolation/gxr-controller-extrapolation-0.2.0-{manual,auto}.zip`
and a SHA-256 checksum file for each ZIP. Pass `--variant manual` or
`--variant auto` to build just one variant.
The build does not connect to the headset or install the module.

## Project rules

Keeping the headset bootable takes priority over experimental features.
Modifications must preserve original system files and have a clear rollback
path. Choose the manual or automatic package explicitly. The automatic variant
uses the failure guards described in [AUTOSTART.md](docs/AUTOSTART.md).
See [AGENTS.md](AGENTS.md) for the complete requirements.

Samsung and Steam Link APKs, device dumps, signing keys, dependencies, and compiled
files are excluded from Git. Module ZIPs are published separately in Releases.
Versions and tags are independent for each module: `haptic-main/v0.3.0`
and `controller-extrapolation/v0.2.0`.
See the [release guidelines](docs/RELEASING.md) and
[original headset validation record](docs/VALIDATION-2026-10-10.md) and
[manual/auto validation record](docs/VALIDATION-2026-10-11-AUTOSTART.md).
