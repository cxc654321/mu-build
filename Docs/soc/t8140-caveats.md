
## Hazards this firmware cannot fix

**Secondary cores lose architectural state in `WFI`, and `WFIT` is unsafe.**
Established on a live J700 by a pre-fix Linux panic with all registers zero, and
worked around there with `idle=nop arm64.nowfxt` — see
aurora-silicon/neo-bringup, `docs/hardware-inventory.md` row "CPU WFx behavior",
graded "A; workaround physically verified".

Windows has no equivalent switch. Its idle path takes secondaries into `WFI`, so
this is expected to be the first thing that goes wrong once more than the boot
CPU is running, and it will not look like a firmware bug. Nothing in this
package can prevent it; it is recorded here so the symptom is recognised rather
than re-diagnosed.

**Firmware locks all six RVBARs to the Stage 1 base.** A chainloaded payload has
to be placed at exactly that address. This is m1n1's problem rather than
UEFI's, but it constrains how anything gets onto the machine.

## What was measured, and what was not

Every address in this package comes from a live J700 via
`DeviceTree/t8140-j700.dts`, which records the evidence for each one. Not
measured, and therefore absent:

- **PCIe.** No capture records a root complex address, so no root bridge is
  described and `NTASI_SOC_HAS_PCIE` is 0. Internal storage does not need it:
  ANS sits at fixed MMIO and `AppleNANDStorageDxe` reads it from the live ADT.
- **Keyboard, trackpad and display.** MTP DAPF initialises on the machine but
  the J700 board tree has no MTP, HID or input nodes, and there is no DCP work.
  J700 therefore uses `GenericBoardPkg`, which claims none of them.
- **SMC.** No battery, thermal or fan telemetry.
- **AIC geometry.** The IRQ counts and register strides a CSRT needs are read
  off the controller at runtime; see `AcpiTables/CSRT.aslc`.
