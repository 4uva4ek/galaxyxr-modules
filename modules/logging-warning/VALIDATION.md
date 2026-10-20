# Local validation — 2026-10-20

- Layout: 27 local modules passed.
- Catalogue: 4 public modules passed (new module not published).
- Repository contract: 16 tracked modules passed. New untracked module was
  separately passed to check_module_contract.check_module and passed too.
- Installer replay: `verify_package.py --contract-only` passed.
- Shell syntax passed Git Bash; the controller and smoke script also passed
  Android `sh -n`.
- Galaxy XR SM-I610: temporary manually invoked copy in
  `/data/local/tmp/gxr-logging-warning-test-20261020-1`, with no boot integration.
  An outer 30-second timeout bounded activation. The smoke test verified global
  WARNING, audio and Spaceflinger tag overrides, suppression of a fresh INFO
  message, retention of fresh WARNING and ERROR messages, and byte-for-byte
  equality of sorted logging properties before/after disable. Journal removed.
- No workload CPU comparison was performed; performance benefit is unquantified.
- Initial installation was blocked by automatic approval review. The user then
  explicitly authorized installation over Wi-Fi. The reviewed GXR installer
  installed version 0.1.0 DISABLED on Galaxy XR; all installed SHA256SUMS passed.
  Action invoked by absolute path reported DISABLED, with no active runtime.
  No enablement or restart was performed. Temporary installer and smoke-test
  files were removed after confirming restoration and absence of a test worker.
- Initial sandbox contract run was blocked by Windows sandbox restrictions on
  Git Bash/temp files; rerun outside the sandbox passed.

Package: `gxr-logging-warning-0.1.0-auto.zip`

SHA256: `063cf20ecabac8d57c79a24134f7a357a264016b1e306122c29eb356ae395ee9`
