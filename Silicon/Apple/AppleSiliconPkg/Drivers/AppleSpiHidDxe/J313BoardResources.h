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
  /* Size is the supplied container span, not a claimed serialized tree size.
   * TreeComplete gates Consumed/TailSize/TailAllZero: only set after the
   * entire counted root tree passes structural checks. Empty tail is all zero.
   * Diagnostics do not change policy: Parse/ParseEx remain strict. */
  size_t Size;
  size_t Consumed;
  size_t TailSize;
  uint32_t TreeComplete;
  uint32_t TailAllZero;
} J313_ADT_DIAGNOSTIC;
int J313BoardResourcesParseEx(const void *Blob, size_t Size,
  J313_SPI_RESOURCES *Resources, J313_ADT_DIAGNOSTIC *Diagnostic);
/* EXPERIMENTAL container policy: fully validate the first counted root, then
 * ignore arbitrary trailing bytes (including a second tree). All identity,
 * address and resource checks still apply exclusively to that first root.
 * No mapping expansion; caller must retain reservation of the full Size span.
 * Same output/diagnostic ABI and failure guarantees as ParseEx. */
int J313BoardResourcesParseContainer(const void *Blob, size_t Size,
  J313_SPI_RESOURCES *Resources, J313_ADT_DIAGNOSTIC *Diagnostic);
/* Identity mismatch bits, independent of the unchanged diagnostic ABI. */
#define J313_IDENTITY_TARGET_TYPE (1u << 0)
#define J313_IDENTITY_CHIP_ID     (1u << 1)
#define J313_IDENTITY_ARM_IO      (1u << 2)
#define J313_IDENTITY_SPI         (1u << 3)
#define J313_IDENTITY_IPD         (1u << 4)
/* J313 Load experiment ONLY; legacy Parse/ParseEx/ParseContainer stay strict.
 * Same tail-ignore policy as ParseContainer. Collect all five missing/mismatched
 * identity checks without rejecting them; never bypass structural, reg/ranges,
 * clock-gates or PMGR checks or synthesize missing nodes/properties/ranges.
 * On success TargetType/ChipId are assumed j313/t8103, NOT verified identity.
 * Optional IdentityMask is reset on entry; zero before successful structural
 * validation does NOT mean identity passed. Bits survive later resource failure.
 * Output pointers must not overlap each other or Blob. Diagnostic ABI unchanged.
 */
int J313BoardResourcesParseExperimentalIdentityBypass(const void *Blob, size_t Size,
  J313_SPI_RESOURCES *Resources, J313_ADT_DIAGNOSTIC *Diagnostic, uint32_t *IdentityMask);
#ifndef J313_HOST_TEST
#include <Uefi.h>
EFI_STATUS J313BoardResourcesLoad(J313_SPI_RESOURCES *Resources);
#endif
#endif
