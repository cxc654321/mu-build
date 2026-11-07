"""Compile the verbatim production initializer with host DT/boot-service mocks.

Uses the real EmbeddedGpio protocol header, not a copied controller layout.
No MMIO, firmware execution, or hardware is involved.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / "Silicon/Apple/AppleSiliconPkg/Drivers/AppleEmbeddedGpioControllerDxe/AppleEmbeddedGpioControllerDxe.c"

MOCK = r"""
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#define IN
#define OUT
#define EFIAPI
#define CONST const
#define VOID void
typedef uintptr_t UINTN;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef uint8_t UINT8;
typedef UINTN EFI_STATUS;
typedef void *EFI_HANDLE;
typedef struct { int unused; } EFI_SYSTEM_TABLE;
typedef struct { int unused; } EFI_GUID;
#define EFI_SUCCESS 0
#define ERR(n) (((UINTN)1 << (sizeof(UINTN) * 8 - 1)) | (n))
#define EFI_NOT_FOUND ERR(14)
#define EFI_DEVICE_ERROR ERR(7)
#define EFI_OUT_OF_RESOURCES ERR(9)
#define EFI_ACCESS_DENIED ERR(15)
#define EFI_ERROR(s) (((s) & ERR(0)) != 0)
#define DEBUG(x) do {} while (0)
#define ASSERT_PROTOCOL_ALREADY_INSTALLED(h, g) do {} while (0)
#define CopyMem memcpy
#include <Protocol/EmbeddedGpio.h>

EFI_GUID gEmbeddedGpioProtocolGuid, gPlatformGpioProtocolGuid;
EMBEDDED_GPIO gGpio;
PLATFORM_GPIO_CONTROLLER *AppleSiliconPlatformGpioController;
typedef struct { int unused; } dt_node_t;
static dt_node_t node;
static unsigned char property[9]; /* Deliberately return an unaligned value. */
static size_t property_length;
static int missing_property, missing_node, reg_result, fail_alloc;
static int alloc_calls, free_calls, reg_calls, install_calls, published;
static void *allocation;
static EFI_STATUS install_status, locate_status;
static GPIO_CONTROLLER existing_controller;
static PLATFORM_GPIO_CONTROLLER existing;
#define BASE UINT64_C(0x123450000)
static UINT32 ArmReadMidr(void) { return 0x61000000; }
static dt_node_t *dt_get(const char *name) {
  assert(!strcmp(name, "gpio") || !strcmp(name, "gpio0"));
  return missing_node ? NULL : &node;
}
static void *dt_node_prop(dt_node_t *n, const char *name, size_t *len) {
  assert(n == &node && !strcmp(name, "#gpio-pins"));
  if (missing_property) return NULL; /* Missing property need not set len. */
  *len = property_length;
  return property + 1;
}
static int dt_node_reg(dt_node_t *n, uint32_t idx, uint64_t *base, uint64_t *size) {
  assert(n == &node && idx == 0 && size == NULL);
  reg_calls++;
  if (reg_result) return reg_result; /* Output intentionally untouched. */
  *base = BASE;
  return 0;
}
static void *AllocateZeroPool(size_t size) {
  alloc_calls++;
  assert(reg_calls == 1 && reg_result == 0);
  assert(size == sizeof(PLATFORM_GPIO_CONTROLLER) + sizeof(GPIO_CONTROLLER));
  if (fail_alloc) return NULL;
  assert(allocation == NULL);
  allocation = calloc(1, size);
  assert(allocation != NULL);
  return allocation;
}
static void FreePool(void *p) {
  assert(p != NULL && p == allocation);
  free_calls++;
  free(p);
  allocation = NULL;
}
static EFI_STATUS LocateProtocol(EFI_GUID *guid, void *registration, void **out) {
  assert(guid == &gPlatformGpioProtocolGuid && registration == NULL);
  if (!EFI_ERROR(locate_status)) *out = &existing;
  return locate_status;
}
static EFI_STATUS InstallMultipleProtocolInterfaces(EFI_HANDLE *handle, ...) {
  va_list ap;
  assert(*handle == NULL && allocation != NULL);
  va_start(ap, handle);
  assert(va_arg(ap, EFI_GUID *) == &gEmbeddedGpioProtocolGuid);
  assert(va_arg(ap, EMBEDDED_GPIO *) == &gGpio);
  assert(va_arg(ap, void *) == NULL);
  va_end(ap);
  install_calls++;
  assert(AppleSiliconPlatformGpioController == allocation);
  assert(AppleSiliconPlatformGpioController->GpioCount == 256);
  assert(AppleSiliconPlatformGpioController->GpioControllerCount == 1);
  GPIO_CONTROLLER *c = AppleSiliconPlatformGpioController->GpioController;
  assert(c == (GPIO_CONTROLLER *)((char *)allocation + sizeof(PLATFORM_GPIO_CONTROLLER)));
  assert(c->GpioIndex == 0 && c->InternalGpioCount == 256 && c->RegisterBase == BASE);
  if (!EFI_ERROR(install_status)) { published++; *handle = &node; }
  return install_status;
}
static struct {
  EFI_STATUS (*LocateProtocol)(EFI_GUID *, void *, void **);
  EFI_STATUS (*InstallMultipleProtocolInterfaces)(EFI_HANDLE *, ...);
} services = { LocateProtocol, InstallMultipleProtocolInterfaces }, *gBS = &services;
"""

MAIN = r"""
static void reset(void) {
  assert(allocation == NULL);
  AppleSiliconPlatformGpioController = NULL;
  UINT32 count = 256;
  memcpy(property + 1, &count, sizeof(count));
  property_length = sizeof(count);
  missing_property = missing_node = reg_result = fail_alloc = 0;
  alloc_calls = free_calls = reg_calls = install_calls = published = 0;
  install_status = EFI_SUCCESS;
  locate_status = EFI_NOT_FOUND;
  existing.GpioController = &existing_controller;
}
static EFI_STATUS run(void) {
  return AppleEmbeddedGpioControllerDxeInitialize(NULL, NULL);
}
static void rejected(EFI_STATUS status, int expected_reg_calls) {
  assert(run() == status);
  assert(reg_calls == expected_reg_calls);
  assert(!alloc_calls && !free_calls && !install_calls && !published);
  assert(AppleSiliconPlatformGpioController == NULL && allocation == NULL);
}
int main(int argc, char **argv) {
  assert(argc == 2);
  reset();
  if (!strcmp(argv[1], "256")) {
    assert(run() == EFI_SUCCESS);
    assert(alloc_calls == 1 && install_calls == 1 && published == 1 && !free_calls);
    FreePool(allocation); /* Test teardown, not driver cleanup on success. */
  } else if (!strcmp(argv[1], "missing-property")) {
    missing_property = 1;
    rejected(EFI_NOT_FOUND, 0);
  } else if (!strcmp(argv[1], "bad-length")) {
    const size_t lengths[] = {0, 1, 2, 3, 5, 8, SIZE_MAX};
    for (size_t i = 0; i < sizeof(lengths)/sizeof(lengths[0]); i++) {
      reset(); property_length = lengths[i]; rejected(EFI_DEVICE_ERROR, 0);
    }
  } else if (!strcmp(argv[1], "zero-count")) {
    memset(property, 0, sizeof(property)); rejected(EFI_DEVICE_ERROR, 0);
  } else if (!strcmp(argv[1], "reg-failure")) {
    reg_result = 1; rejected(EFI_DEVICE_ERROR, 1);
    reset(); reg_result = -1; rejected(EFI_DEVICE_ERROR, 1);
  } else if (!strcmp(argv[1], "allocation-failure")) {
    fail_alloc = 1;
    assert(run() == EFI_OUT_OF_RESOURCES);
    assert(alloc_calls == 1 && !free_calls && !install_calls && !published);
    assert(allocation == NULL && AppleSiliconPlatformGpioController == NULL);
  } else if (!strcmp(argv[1], "install-failure")) {
    install_status = EFI_ACCESS_DENIED;
    assert(run() == EFI_ACCESS_DENIED);
    assert(alloc_calls == 1 && free_calls == 1 && install_calls == 1 && !published);
    assert(allocation == NULL && AppleSiliconPlatformGpioController == NULL);
  } else if (!strcmp(argv[1], "missing-node")) {
    missing_node = 1; rejected(EFI_NOT_FOUND, 0);
  } else if (!strcmp(argv[1], "existing-platform")) {
    locate_status = EFI_SUCCESS;
    assert(run() == EFI_SUCCESS);
    assert(AppleSiliconPlatformGpioController == &existing);
    assert(!alloc_calls && !free_calls && !reg_calls && !install_calls);
  } else if (!strcmp(argv[1], "locate-failure")) {
    locate_status = EFI_DEVICE_ERROR; rejected(EFI_DEVICE_ERROR, 0);
  } else { assert(0); }
  puts("PASS");
  return 0;
}
"""


class AppleGpioInitTests(unittest.TestCase):
    def test_production_initializer(self):
        source = DRIVER.read_text()
        start = source.index("EFI_STATUS\nEFIAPI \nAppleEmbeddedGpioControllerDxeInitialize(")
        end = source.index("\n}", start) + 2
        initializer = source[start:end]
        with tempfile.TemporaryDirectory(prefix="apple-gpio-init-") as tmp:
            cfile = Path(tmp) / "init.c"
            binary = Path(tmp) / "init"
            cfile.write_text(MOCK + initializer + MAIN)
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-Wno-unused-parameter", "-fsanitize=address,undefined",
                "-fno-omit-frame-pointer",
                "-I", str(ROOT / "Common/TIANO/EmbeddedPkg/Include"),
                str(cfile), "-o", str(binary),
            ], check=True, capture_output=True, text=True)
            for case in (
                "256", "missing-property", "bad-length", "zero-count",
                "reg-failure", "allocation-failure", "install-failure",
                "missing-node", "existing-platform", "locate-failure",
            ):
                with self.subTest(case=case):
                    result = subprocess.run([str(binary), case], check=True,
                                            capture_output=True, text=True)
                    self.assertEqual(result.stdout.strip(), "PASS")


if __name__ == "__main__":
    unittest.main()
