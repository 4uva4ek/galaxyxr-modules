# Independent module releases

1. Update `version` and `versionCode` in the selected module's `module.prop`.
2. Update its README and compatibility/validation record.
3. Build from a clean Git checkout using the selected module's documented command.
   The shared builder is `python tools/build.py NAME --ndk PATH`. Before building
   Controller Extrapolation, run `python modules/openxr-velocity-frame/prepare.py
   --ndk PATH --runtime /path/to/libopenxr_android.so` with your own pinned runtime;
   this creates the ignored shared compatibility header. OpenXR Velocity Frame's
   dedicated builder runs this preparation itself. Do not publish generated vendor
   function bytes or replace the pinned firmware hash to make a build pass.
4. Run package checks and the required device checks. Distinguish build success,
   software activation/rollback, and user confirmation of the physical effect.
5. Create the tag `NAME/vVERSION` on the verified commit.
6. Create a GitHub Release for that tag and attach only `-auto.zip` and its
   `.zip.sha256` file from `dist/NAME/`. Verify the archive contains an empty
   `disable` marker and only the standard `service.sh` startup entry.
   Do not publish manual packages or restore their obsolete download links.

Do not commit build artifacts, APKs, dumps, signing keys, or device data.
Automatic packages install disabled. Updating an enabled installation requires
restoring and disabling it first; deployment never authorizes activation.
Local/native testing remains required before deployment, without a manual ZIP.
Automatic packages use standard KernelSU hooks and must retain failure latching;
do not alter root acquisition or system partitions. Supporting new firmware
requires fresh analysis and precise compatibility checks; do not remove existing
hash checks to accept an unknown build.
Releases are published manually. A push does not automatically publish a release.
