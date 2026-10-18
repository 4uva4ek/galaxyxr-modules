# Galaxy XR Steam Link Suite

## 0.1.6 - 2026-10-19

- Remove Controller Extrapolation from the suite (component, `EXTRAPOLATION` setting and WebUI switch). The standalone `gxr_controller_extrapolation` module is still restored and removed during migration. This also removes the update refusal when two clients had the OpenXR runtime loaded.
- A KernelSU soft restart while the suite is active is now a planned handoff instead of a suite failure. The supervisor stops on the restart gate (`busy` or a new generation), restores all components and clears its pending marker without a failure guard; the service stage after the restart starts the suite again with the checkbox unchanged.
- App Priorities no longer fails with "Wakefulness unavailable" (or another framework query error) while Android is restarting: it restores owned priorities and exits as a handoff. Outside a restart the same errors still quarantine it.
- Pose Time stops as a handoff when the restart begins and tolerates the force-stop race only after its hook is verified restored or its process is gone.
- The suite keeps the first failure reason (for example the failing component) instead of overwriting it with a generic worker message.
- 0.1.5 was not published separately; its changes ship in 0.1.6. Device validation of an active-to-soft-restart cycle is pending.

## 0.1.4 - 2026-10-19

- Pose Time supports Steam Link 2.0.20 and 2.0.23 through per-version scene profiles (GetControllerState, SubmitMessage and vendor clock locations), selected by live code bytes. Renamed and patched copies such as `.x_23` match when those functions are unchanged.
- Recovery records name their scene profile; records from earlier builds are read as 2.0.20.
- Controller Extrapolation status/off report WAITING (no live patch) for clients that have not loaded the OpenXR runtime, such as a background Steam Link, instead of failing; this no longer blocks updates.
- Device check after a cold boot: Velocity Frame corrected main Steam Link and `.x_23` at the same time; Pose Time was live on `.x_23` with the 2.0.23 profile (about 11.5k packets matched and relabelled per hand). No tracking-quality or latency measurement.
- Known limit: with two clients that both have the OpenXR runtime loaded, Controller Extrapolation status reports an error instead of a per-client state; no writes are made, but a later update is refused until one client is closed.

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
