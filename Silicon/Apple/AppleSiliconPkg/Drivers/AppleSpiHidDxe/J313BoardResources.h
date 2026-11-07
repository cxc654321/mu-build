/* SPDX-License-Identifier: MIT */
#ifndef J313_BOARD_RESOURCES_H
#define J313_BOARD_RESOURCES_H
#include "J313SpiTransport.h"
/* Zero output on failure. Pure bounded parser; no MMIO, no ownership claim.
 * Returns 0 on success, nonzero on invalid/unsupported ADT. */
int J313BoardResourcesParse(const void *Blob, size_t Size, J313_SPI_RESOURCES *Resources);
#ifndef J313_HOST_TEST
#include <Uefi.h>
EFI_STATUS J313BoardResourcesLoad(J313_SPI_RESOURCES *Resources);
#endif
#endif
