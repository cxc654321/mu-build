/* SPDX-License-Identifier: MIT */
#ifndef J313_BOARD_RESOURCES_H
#define J313_BOARD_RESOURCES_H
#include "J313SpiTransport.h"
/* Zero output on failure. Pure bounded parser; no MMIO, no ownership claim.
 * Returns 0 on success, nonzero on invalid/unsupported ADT. */
int J313BoardResourcesParse(const void *Blob, size_t Size, J313_SPI_RESOURCES *Resources);
/* Stable numeric structural failure reasons; offsets are blob-relative.
 * NodeIndex is preorder, SIZE_MAX when unavailable. Property is sanitized ASCII,
 * at most 32 bytes plus NUL; never contains property values or node names.
 * On success Reason=0, Offset=consumed bytes, NodeIndex=SIZE_MAX.
 * Semantic failures retain the legacy return code and Reason=0.
 * Diagnostics must not overlap blob or output. */
typedef enum {
  J313_ADT_OK = 0, J313_ADT_ARGUMENT, J313_ADT_SIZE_BUDGET,
  J313_ADT_FDT, J313_ADT_DEPTH_BUDGET, J313_ADT_NODE_BUDGET,
  J313_ADT_NODE_HEADER, J313_ADT_COUNT_BUDGET, J313_ADT_PROPERTY_HEADER,
  J313_ADT_PROPERTY_KEY, J313_ADT_DUPLICATE_PROPERTY, J313_ADT_VALUE_LENGTH,
  J313_ADT_VALUE_PADDING, J313_ADT_NAME_TEMPLATE, J313_ADT_NAME_VALUE,
  J313_ADT_NAME_MISSING, J313_ADT_DUPLICATE_NODE, J313_ADT_TRAILING_BYTES,
  J313_ADT_ALLOCATION
} J313_ADT_REASON;
typedef struct {
  uint32_t Reason;
  size_t Offset;
  size_t NodeIndex;
  char Property[33];
} J313_ADT_DIAGNOSTIC;
int J313BoardResourcesParseEx(const void *Blob, size_t Size,
  J313_SPI_RESOURCES *Resources, J313_ADT_DIAGNOSTIC *Diagnostic);
#ifndef J313_HOST_TEST
#include <Uefi.h>
EFI_STATUS J313BoardResourcesLoad(J313_SPI_RESOURCES *Resources);
#endif
#endif
