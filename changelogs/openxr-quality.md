# GXR OpenXR QUALITY

## 0.1.1

- First public experimental release. Hold HEAD_TRACKING QUALITY while an awake
  headset has a visible OpenXR app; release on sleep, app exit or critical heat.
- Preserve exact firmware/ABI checks, late-load restart gate, independent guardian,
  restoration, same-boot failure containment and cold-boot guard reset.
- Add Manager-compatible off/status commands. Off waits for confirmed stop;
  status reports a safe-update marker only with no controller or owned override.
- Native controller unchanged from tested local 0.1.0. Author: 4uva4ek.
- Ships disabled. Enablement and soft restart are separate user actions.

Local 0.1.0 requires reviewed direct migration; its Action script does not
implement the update contract. See the module README before replacing it.
Visible-app minimize/restore, sustained camera rates, subjective improvement
and full power-cycle testing remain unvalidated for this module. The mode
request does not establish sensor rate or solve every tracking issue.

See [release validation](../docs/RELEASE-QUALITY-WHITENING-2026-10-13.md).
