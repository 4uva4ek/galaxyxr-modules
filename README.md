# Galaxy XR Modules

Independent KernelSU modules for Samsung Galaxy XR in one repository.
Each module has its own version and ZIP package. Build tools and device safety
rules are shared across the repository.

| Module | Purpose | Version |
| --- | --- | --- |
| [haptic-main](modules/haptic-main/README.md) | Manually route SUB haptics to MAIN by modifying service memory | 0.2.0 |

## Repository layout

```text
modules/haptic-main/    source, module.prop, validation, and instructions
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
```

Output: `dist/haptic-main/gxr-haptic-main-0.2.0.zip` and a SHA-256 checksum file.
The build does not connect to the headset or install the module.

## Project rules

Keeping the headset bootable takes priority over experimental features.
Modifications must preserve original system files and have a clear rollback
path. The current module requires manual activation and does not run at boot.
See [AGENTS.md](AGENTS.md) for the complete requirements.

Samsung and Steam Link APKs, device dumps, signing keys, dependencies, and compiled
files are excluded from Git. Module ZIPs are published separately in Releases.
Versions and tags are independent for each module: `haptic-main/v0.2.0`.
See the [release guidelines](docs/RELEASING.md) and
[headset validation record](docs/VALIDATION-2026-10-10.md).
