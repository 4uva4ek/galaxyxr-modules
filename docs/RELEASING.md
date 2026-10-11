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
   `disable` marker and the documented startup entries. Ordinary modules use `service.sh`;
   optional planned-restart notification hooks are permitted. Coordinated
   participants require the complete four-hook contract documented in
   `COORDINATED-RESTART-2026-10-11.md`. Do not add an early-boot activation path.
   Do not publish manual packages or restore their obsolete download links.

Framebuffer10 is currently source-only. Its ignored local compositor payload
and ZIP contain proprietary vendor code and must not be uploaded. An automatic
ZIP is not automatically eligible for public redistribution.

Do not commit build artifacts, APKs, dumps, signing keys, or device data.
Automatic packages install disabled. Updating an enabled installation requires
restoring and disabling it first; deployment never authorizes activation.
Local/native testing remains required before deployment, without a manual ZIP.
Automatic packages use standard KernelSU hooks and must retain failure latching;
do not alter root acquisition or system partitions. Supporting new firmware
requires fresh analysis and precise compatibility checks; do not remove existing
hash checks to accept an unknown build.
Releases are published manually. A push does not automatically publish a release.
