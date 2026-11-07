/* SPDX-License-Identifier: MIT
 * Firmware-only C vocabulary shim. No host C library is linked into DXE.
 */
#ifndef J313_CORE_COMPAT_H
#define J313_CORE_COMPAT_H
#ifdef J313_HOST_TEST
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#else
#include <Base.h>
#include <Library/BaseMemoryLib.h>
typedef UINT8 uint8_t;
typedef UINT16 uint16_t;
typedef UINT32 uint32_t;
typedef UINT64 uint64_t;
typedef INT8 int8_t;
typedef INT16 int16_t;
typedef INT32 int32_t;
typedef INT64 int64_t;
typedef UINTN uintptr_t;
typedef UINTN size_t;
typedef BOOLEAN bool;
#define true TRUE
#define false FALSE
#define UINT32_C(x) x##U
#define UINT64_C(x) x##ULL
#define UINT32_MAX MAX_UINT32
#define UINT64_MAX MAX_UINT64
#define SIZE_MAX MAX_UINTN
#define memcpy(d,s,n) CopyMem((d),(s),(n))
#define memset(d,v,n) SetMem((d),(n),(UINT8)(v))
#define memcmp(a,b,n) CompareMem((a),(b),(n))
#endif
#endif
