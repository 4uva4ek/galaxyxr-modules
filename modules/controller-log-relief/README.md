# Controller HAL Log Relief

Author: 4uva4ek. Internal suite component, not offered in Sources.

Guarded controller HAL LogLevel=0. Internal Controller Suite component.

Automatic-only and installed disabled. Original files and partitions remain unchanged.
Build with `python tools/build.py controller-log-relief --ndk PATH` (NDK 28.2.13676358).

Preserves the prior Stream CPU Relief HAL mechanism: exact vendor library hash,
private mapping/inode, original gate value and adjacent callback guards; only the
four-byte LogLevel data is written in process RAM. No thread priorities are changed.
The original gate is restored on disable. External LogLevel=0 is not adopted.
HAL restart or unresolved restore disables the component and preserves evidence.
The parent suite owns activation; update both suites disabled when migrating from
0.1.1 so the old Steam Link Suite relinquishes its HAL ownership first.
