# Galaxy XR Steam Link Suite

## 0.1.4 - 2026-10-19

- Pose Time supports Steam Link 2.0.20 and 2.0.23 through per-version scene profiles (GetControllerState, SubmitMessage and vendor clock locations), selected by live code bytes. Renamed and patched copies such as `.x_23` match when those functions are unchanged.
- Recovery records name their scene profile; records from earlier builds are read as 2.0.20.
- Controller Extrapolation status/off report WAITING (no live patch) for clients that have not loaded the OpenXR runtime, such as a background Steam Link, instead of failing; this no longer blocks updates.

## 0.1.3 - 2026-10-19

- Target only streaming clients: packages starting with `com.valvesoftware.steamlinkvr` (Steam Link and copies such as `.x_23`) or `dev.chronolink.client` (ChronoLink). Other OpenXR apps are no longer corrected.
- Velocity Frame and Controller Extrapolation accept Steam Link copies and ChronoLink; their OpenXR runtime pin is unchanged.
- Pose Time accepts Steam Link copies and still requires the supported 2.0.20 scene code; with several copies running it selects the supported one and leaves the others unchanged.
- The app priority profile covers Steam Link copies. ChronoLink has no thread profile yet and is reported unchanged.

## 0.1.2 - 2026-10-18

- Move HAL-wide LogLevel=0 ownership to Controller Suite.
- Split app priority management into a visible-OpenXR profile mechanism; Steam Link is the initial supported profile.
- Restore priorities on background/sleep and exclude ambiguous Thread-9.
- Update both suites disabled before enabling. Soft-restart startup, HAL ownership and active Steam Link priorities verified on device.

## 0.1.1 - 2026-10-18

- First public suite release, consolidating the existing components under one activation preference and WebUI.
- Declare replaced modules for reviewed migration in KernelSU GXR 1.0.11 or later; install disabled.
- English/Russian WebUI follows the headset locale, with a manual language override.
- Preserve existing component tuning, compatibility checks and recovery ownership.

See the module README for components, migration and validation limits.
