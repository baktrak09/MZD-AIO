# Factory Android Auto touchscreen patch (FW 70.00.335C)

Status: **experimental, integration in progress; not ready for vehicle installation**.

This project adds touchscreen input to **factory Android Auto**, without installing or replacing the MZD-AIO Android Auto Headunit App.

## Boundaries

- Target firmware: Mazda Connect 70.00.335C.
- Inject a touchscreen-only ARM32 shared library into the `jciAAPA` service through an `LD_PRELOAD` environment entry in `/jci/sm/sm.conf`.
- Preserve factory audio, steering-wheel controls, and the factory Android Auto stack.
- Provide separate MZD-AIO USB install and uninstall choices.
- Keep a recoverable original configuration and avoid altering unrelated service entries.

## Existing local development payload

The ARM library and shell scripts were built outside this GitHub repository in:

`~/mazda-touch-port/touch-only/package/`

The compiled library `files/libmazda_touch.so` **has not yet been imported into GitHub**. The repository cannot produce a working USB installer until the library is provided and its integrity is checked.

## Validation still required

1. Connect the new tweak to the MZD-AIO GUI selection and USB generation workflow, rather than the existing `25_androidauto-*.txt` scripts.
2. Import the ARM library and matching installer/rollback payload.
3. Validate generated USB install and uninstall scripts in a local test harness.
4. Test on the target CMU. Firmware-specific function offsets and signatures have not been independently validated against the live FW70 library.
5. Keep serial-root access available for emergency recovery.

**Do not deploy a partially integrated installer to the vehicle.**
