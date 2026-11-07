# Production J313 SPI transport

This transport has real UEFI IoLib/TimerLib callbacks and separately injected
mock callbacks. Nothing here was executed against hardware; no flashing occurred.
Mock success is not evidence of working keys, measured SCK, or reaching Mu GUI.

## Provenance (do not relicense dependencies)

- GPL-2.0-only PIO engine is reused via [production core](Core/j313_spi_pio.c),
  from the tested experimental algorithm, with its original provenance retained.
  [Asahi SPI controller, fixed 549e8b4](https://github.com/AsahiLinux/linux/blob/549e8b442b485c03e3e766635e2203c75362e312/drivers/spi/spi-apple.c)
  supplies register sequencing. Quarantined driver code was NOT enabled/copied.
- [GPIO driver, fixed 414e55d](https://github.com/AsahiLinux/linux/blob/414e55da7a0afaca2a97b29603be88ce0a6fd6de/drivers/pinctrl/pinctrl-apple-gpio.c):
  GPL-2.0-only, Asahi contributors, Corellium LLC; based on Imagination/Google
  pinctrl. PERIPH[6:5], INPUT_ENABLE[9], MODE[3:1], DATA[0], LOCK[21]; mux function
  1 plus input-enable. Polling NUB13 uses mode7 (IRQ OFF), NOT low-level IRQ mode3.
- [OF glue, fixed 549e8b4](https://github.com/AsahiLinux/linux/blob/549e8b442b485c03e3e766635e2203c75362e312/drivers/hid/spi-hid/spi-hid-apple-of.c):
  GPL-2.0, Asahi contributors. Enable high 5ms / low 5ms / high 50ms.
- [HID core, fixed 549e8b4](https://github.com/AsahiLinux/linux/blob/549e8b442b485c03e3e766635e2203c75362e312/drivers/hid/spi-hid/spi-hid-apple-core.c):
  GPL-2.0. TX256, 200us gap, RX4 ACK ac2768d5 under one CS; independent RX256.
- [m1n1 PMGR, locally pinned 69affa5](https://github.com/AsahiLinux/m1n1/blob/69affa5a867a44cb281c6cf8716f5a38e3b0b063/src/pmgr.c):
  local file explicitly MIT. Lines84-95 supply target/actual mask+poll approach.
  Clear AUTO_ENABLE/WAS_CLK_GATED/WAS_PWR_GATED/target; request target15; wait
  actual15. This is a different revision/license from the older GPL m1n1 snapshot
  listed in the historical research document. This combined adaptation is GPL-2.0-only.
- [J313 board DTS, fixed 414e55d](https://github.com/AsahiLinux/linux/blob/414e55da7a0afaca2a97b29603be88ce0a6fd6de/arch/arm64/boot/dts/apple/t8103-j313.dts):
  GPL-2.0+ OR MIT: AP46-49 mux1, AP195 active-high enable, NUB13 active-low ready,
  CS0, 65us setup/hold and 250us inactive.
- [T8103 DTS, fixed 414e55d](https://github.com/AsahiLinux/linux/blob/414e55da7a0afaca2a97b29603be88ce0a6fd6de/arch/arm64/boot/dts/apple/t8103.dtsi)
  and [power domains](https://github.com/AsahiLinux/linux/blob/414e55da7a0afaca2a97b29603be88ce0a6fd6de/arch/arm64/boot/dts/apple/t8103-pmgr.dtsi):
  dual MIT/GPL. SPI3 base23510c000, clock120MHz; PMGR main23b700000 size14000,
  AP23c100000 size100000, NUB23d1f0000 size4000, mini23d280000 size4000.
  AP GPIO PS offset130; NUB GPIO mini offset80; SPI3 PS offset258. Actual raw ADT
  clock-gate resolution (including 0x34) is the board adapter's responsibility.

## Contract and failure behavior

The board adapter must derive identity, mapping, and all dependency DAGs from
actual runtime ADT. Transport checks exact j313/T8103 identity, physical ranges,
16-node cap, duplicate/alignment/window constraints, both GPIO nodes, and SPI3
last before **any** MMIO read or write. Main/mini PMGR envelope gaps are rejected.
Caller holds exclusive serialized ownership and Device mappings throughout.
Init powers every merged dependency before GPIO or SPI access. PMGR transitions
are bounded to10ms/node; device reset60ms. Each packet transaction is bounded
by5ms plus unconditional cleanup and a20000-step fallback for frozen clocks.
Callback return and hardware bus accessibility remain platform obligations.
Stop/failure stops RUN, releases CS, disables SPI IRQs and deasserts enable;
PMGR remains on because shared-parent gating would affect unrelated clients.
No automatic retry/replay, no partial RX publication, no fake input protocol.

Raw divider120 is conservative <=1MHz with both published raw and raw+1
interpretations at120MHz. It is not a claim of measured or validated waveform.

## Checks

From repository root:

    python3 -m unittest discover -s Tests -p test_j313_spi_transport.py -v

Compiles production transport + production PIO with ASan/UBSan, warnings as
errors. Tests exercise identity rejection with zero reads/writes, register
window gaps/edges, GPIO power presence, each PMGR failure stage, GPIO lock,
enable timing, CS-continuous TX/ACK, RX integrity, ACK failure, frozen/reversed
clock, timeout cleanup, unchanged output on failed RX and idempotent Stop.
The standalone AArch64 freestanding UEFI syntax check also passed using actual
MdePkg headers; full DSC/INF linkage and hardware validation belong to integration.
