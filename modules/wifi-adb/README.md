# Wi-Fi ADB

Author: **4uva4ek**.

KernelSU GXR module that enables classic authenticated ADB TCP on port 5555
at the service stage. This is not Android's wireless pairing-code interface.
USB debugging must already be enabled and adbd running. Approve the computer's
RSA prompt on the device. Connect from the same network with
`adb connect DEVICE_IP:5555`. ADB may listen on other network interfaces too;
use a trusted network. The module does not turn the Wi-Fi radio on.

Install the ZIP with KernelSU GXR's reviewed installer, enable the module, and
use the supported module restart workflow. Packages install disabled under this
workspace's rules; generic KernelSU installers are intentionally rejected.
Action displays the live listener check and recovery instructions in English.
Disabling a running module restores the previous volatile TCP property within
a few seconds. ADB connections briefly drop on activation/restoration.

Only `service.adb.tcp.port` and the adbd restart control property are changed.
No persistent Android settings, authentication flags, partitions or original
system files are modified. Module-local runtime files remain under its folder.
Failure disables the module, attempts restoration, and blocks retries until a
full kernel boot. A full reboot with the module disabled removes its changes.
If the prior configuration already enabled network ADB, restoration preserves it.

Build from the repository root:

```text
python tools/check_module_layout.py
python modules/wifi-adb/build.py
```

Output: `dist/wifi-adb/gxr-wifi-adb-1.0.0-auto.zip`.
The update URL is reserved and unpublished. On 2026-10-20, version 1.0.0 was
installed disabled on a Galaxy XR using the reviewed GXR installer. All installed
checksums matched and Action confirmed the disabled state. Activation and network
connectivity have not been tested on the device; host checks cannot prove vendor
SELinux compatibility. The 1.0.0 version identifies this initial package and does
not imply validated runtime stability.

Host validation passed: module layout, package contract, shell syntax and mocked
activation/disable restoration, insecure-ADB refusal and restart-failure rollback.

References:
- https://kernelsu.org/guide/module.html
- https://android.googlesource.com/platform/packages/modules/adb/+/refs/heads/main/daemon/main.cpp
