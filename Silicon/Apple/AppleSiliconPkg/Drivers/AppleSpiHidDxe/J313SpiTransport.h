/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef J313_SPI_TRANSPORT_H
#define J313_SPI_TRANSPORT_H
#include "Core/J313CoreCompat.h"
#include "Core/j313_spi_pio.h"

#define J313_SPI_POWER_MAX 16u
#define J313_SPI_PACKET_SIZE 256u
/* Produced by the runtime ADT adapter, not by a public-DTS default initializer.
 * Identity is exact ADT target-type j313 / chip-id 0x8103. All addresses are
 * CPU physical identity-mapped Device memory; caller owns SPI/AP pins/NUB13
 * exclusively. PMGR list includes AP GPIO and NUB GPIO dependencies FIRST,
 * then the ADT-resolved deduplicated parent-first dependency closure for
 * SPI3 clock-gates 0x34, including SPI3 last. No virtual devices in list.
 * PmgrBase=0x23b700000; PmgrSize is an envelope covering both main PMGR and
 * mini PMGR at 0x23d280000. Gaps are NOT treated as accessible registers.
 * Offsets are relative to PMGR base, not clock-gate IDs or Linux phandles.
 */
typedef struct {
  char TargetType[16];
  uint32_t ChipId;
  uint64_t SpiBase, SpiSize, ApBase, ApSize, NubBase, NubSize;
  uint64_t PmgrBase, PmgrSize;
  uint32_t ClockGate, PowerCount;
  uint32_t PowerOffset[J313_SPI_POWER_MAX];
} J313_SPI_RESOURCES;

typedef struct {
  void *Context;
  uint32_t (*Read32)(void *, uint64_t);
  void (*Write32)(void *, uint64_t, uint32_t);
  uint64_t (*NowUs)(void *);
  void (*DelayUs)(void *, uint32_t);
} J313_SPI_IO;

typedef enum {
  J313_SPI_OK, J313_SPI_INVALID, J313_SPI_NOT_READY, J313_SPI_TIMEOUT,
  J313_SPI_IO_ERROR, J313_SPI_ACK_ERROR, J313_SPI_BUSY
} J313_SPI_STATUS;

typedef struct {
  J313_SPI_RESOURCES Resources;
  J313_SPI_IO Io;
  struct pio_result LastTransfer;
  bool Initialized, Busy, ControllerLive, GpioLive;
} J313_SPI_TRANSPORT;

/* Zero-initialize context before first use. Serialized Boot Services only;
 * Init waits <= ~220ms; packet transactions <= 5ms. Never at TPL_HIGH_LEVEL.
 * NULL Io selects real UEFI MMIO/TimerLib callbacks (unavailable in host tests).
 * Resource validation happens before the FIRST read as well as first write.
 */
J313_SPI_STATUS J313SpiTransportInit(J313_SPI_TRANSPORT *, const J313_SPI_RESOURCES *, const J313_SPI_IO *);
J313_SPI_STATUS J313SpiTransportReady(J313_SPI_TRANSPORT *, bool *);
J313_SPI_STATUS J313SpiTransportReadPacket(J313_SPI_TRANSPORT *, uint8_t [J313_SPI_PACKET_SIZE]);
J313_SPI_STATUS J313SpiTransportWritePacket(J313_SPI_TRANSPORT *, const uint8_t [J313_SPI_PACKET_SIZE]);
/* Stops RUN, releases CS, disables SPI IRQs, deasserts enable. Leaves PMGR
 * domains ON (shared parents must not be gated behind other drivers' backs).
 * Does not restore old mux or enable state and never replays old packets. */
void J313SpiTransportStop(J313_SPI_TRANSPORT *);
#endif
