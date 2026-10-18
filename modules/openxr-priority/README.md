# OpenXR App Priority

Author: 4uva4ek. Internal suite component, not offered in Sources.

Profile-based priority for visible third-party OpenXR apps. Internal suite component.

Automatic-only and installed disabled. Original files and partitions remain unchanged.
Build with `python tools/build.py openxr-priority --ndk PATH` (NDK 28.2.13676358).

Discovery requires a visible CPM client, an awake headset, a unique third-party
package UID, the matching main process, and an executable mapping of the system
OpenXR runtime. Package names alone do not authorize scheduling changes.
The initial Steam Link profile selects SVLRXThreadUDP, SVLMediaThread,
NDK MediaCodec_ and CodecLooper; generic Thread-9 and realtime policies are excluded.
Unknown applications are reported without changes. This is not CPU/GPU BOOST.

The root-only /dev journal records original priorities before mutation. Background,
sleep and disable restore owned priorities; exit/reuse does not write to replacement
threads. External conflicts retain recovery evidence and disable the component.
An unreadable process identity is not treated as a successful restoration.
Numeric TID setpriority still has a small check-to-use race; no atomic TID handle is
claimed. Unexpected kill can leave priorities until explicit recovery or app exit.
The parent suite supervises the component and owns the activation checkbox.

Host test (Linux):
`g++ -std=c++17 -Wall -Wextra -Werror modules/openxr-priority/tests/policy_test.cpp -o /tmp/gxr-priority-test && /tmp/gxr-priority-test`
No real priorities or device processes are changed by this test.
