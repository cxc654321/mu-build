"""Compile the actual production GPIO helpers with mocked MMIO; no hardware.

The functions under test are sliced verbatim from
Silicon/Apple/AppleSiliconPkg/Drivers/AppleEmbeddedGpioControllerDxe/
AppleEmbeddedGpioControllerDxe.c so the test cannot drift from a retyped copy.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC_PATH = ROOT / "Silicon/Apple/AppleSiliconPkg/Drivers/AppleEmbeddedGpioControllerDxe/AppleEmbeddedGpioControllerDxe.c"
SRC = SRC_PATH.read_text()


def extract(anchor: str) -> str:
    start = SRC.index(anchor)
    end = SRC.index("\n}", start) + 2
    return SRC[start:end]


FUNCS = "\n".join([
    extract("EFI_STATUS AppleEmbeddedGpioControllerLocate"),
    extract("STATIC UINTN EFIAPI AppleEmbeddedGpioControllerGetRegister"),
    extract("STATIC VOID EFIAPI AppleEmbeddedGpioControllerSetRegister"),
    extract("EFI_STATUS EFIAPI AppleEmbeddedGpioGetGpio"),
    extract("EFI_STATUS EFIAPI AppleEmbeddedGpioSetGpio"),
    extract("EFI_STATUS EFIAPI AppleEmbeddedGpioControllerGetMode"),
])

MOCK = r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define STATIC static
#define VOID void
#define IN
#define OUT
#define EFIAPI
typedef uint64_t EFI_STATUS;
typedef uint64_t UINTN;
typedef uint32_t UINT32;
typedef UINTN EMBEDDED_GPIO_PIN;
typedef struct { int unused; } EMBEDDED_GPIO;
typedef enum { GPIO_MODE_INPUT = 0, GPIO_MODE_OUTPUT_0 = 0x0e, GPIO_MODE_OUTPUT_1 = 0x0f } EMBEDDED_GPIO_MODE;
#define EFI_SUCCESS 0
#define EFI_UNSUPPORTED (UINT64_C(0x8000000000000000) | 3)
#define EFI_INVALID_PARAMETER (UINT64_C(0x8000000000000000) | 2)
#define EFI_NOT_FOUND (UINT64_C(0x8000000000000000) | 14)
#define EFI_ERROR(s) (((s) & UINT64_C(0x8000000000000000)) != 0)
#define FALSE 0
#define TRUE 1
#define DEBUG(x) do {} while (0)
static int assert_calls;
#define ASSERT(x) do { assert_calls++; (void)(x); } while (0)
#include "AppleEmbeddedGpioControllerDxe.h"

/* Mock MMIO: array indexed by addr>>2, with read/write counters. */
#define MEM_WORDS 0x40000
static uint32_t mem[MEM_WORDS];
static int reads, writes;
static uint64_t last_write_addr;
static uint32_t last_write_val;
static UINT32 MmioRead32(UINTN addr) {
  assert((addr & 3) == 0 && (addr >> 2) < MEM_WORDS);
  reads++;
  return mem[addr >> 2];
}
static VOID MmioWrite32(UINTN addr, UINT32 val) {
  assert((addr & 3) == 0 && (addr >> 2) < MEM_WORDS);
  writes++; last_write_addr = addr; last_write_val = val;
  mem[addr >> 2] = val;
}

/* Mirror of MU_BASECORE/EmbeddedPkg/Include/Protocol/EmbeddedGpio.h layout. */
typedef struct { UINTN RegisterBase; UINTN GpioIndex; UINTN InternalGpioCount; } GPIO_CONTROLLER;
typedef struct { UINTN GpioCount; UINTN GpioControllerCount; GPIO_CONTROLLER *GpioController; } PLATFORM_GPIO_CONTROLLER;
PLATFORM_GPIO_CONTROLLER *AppleSiliconPlatformGpioController;
"""

MAIN = r"""
#define BASE0 0x40000ull
#define BASE1 0x80000ull
static GPIO_CONTROLLER ctrls[3] = {
  { BASE0, 0, 256 },
  { BASE1, 260, 32 },
  { 0, ~0ull - 4, 8 }, /* end wraps around UINT64_MAX */
};
static PLATFORM_GPIO_CONTROLLER plat = { 0, 3, ctrls };

int main(void) {
  UINTN idx, off, base, val;
  EMBEDDED_GPIO_MODE mode;
  EMBEDDED_GPIO dummy = {0};
  AppleSiliconPlatformGpioController = &plat;

  /* Miss on controller 0 must not fail the search: 273 is controller 1 pin 13.
     The old code returned EFI_NOT_FOUND at the first miss, and on a hit it
     fell through to the next miss and overwrote EFI_SUCCESS. */
  assert(AppleEmbeddedGpioControllerLocate(273, &idx, &off, &base) == EFI_SUCCESS);
  assert(idx == 1 && off == 13 && base == BASE1);
  /* Old offset formula used Gpio % InternalGpioCount: 273 % 32 = 17, wrong. */
  assert(AppleEmbeddedGpioControllerLocate(100, &idx, &off, &base) == EFI_SUCCESS);
  assert(idx == 0 && off == 100 && base == BASE0);
  assert(AppleEmbeddedGpioControllerLocate(259, &idx, &off, &base) == EFI_NOT_FOUND);
  /* A gap pin (in no controller) stays NOT_FOUND even with a controller whose
     Base+Count would wrap around UINT64_MAX; the subtraction form computes
     range membership without ever forming Base+Count. */
  assert(AppleEmbeddedGpioControllerLocate(1000, &idx, &off, &base) == EFI_NOT_FOUND);
  assert(AppleEmbeddedGpioControllerLocate(~0ull - 2, &idx, &off, &base) == EFI_SUCCESS);
  assert(idx == 2 && off == 2);

  /* Boundary and empty-table cases; failure leaves outputs unchanged. */
  const UINTN valid_pins[] = {0, 255, 260, 291};
  const UINTN valid_offsets[] = {0, 255, 0, 31};
  for (unsigned n = 0; n < 4; ++n) {
    assert(AppleEmbeddedGpioControllerLocate(valid_pins[n], &idx, &off, &base) == EFI_SUCCESS);
    assert(off == valid_offsets[n]);
  }
  const UINTN gaps[] = {256, 259, 292};
  for (unsigned n = 0; n < 3; ++n) {
    idx=17; off=18; base=19;
    assert(AppleEmbeddedGpioControllerLocate(gaps[n], &idx, &off, &base) == EFI_NOT_FOUND);
    assert(idx==17 && off==18 && base==19);
  }
  plat.GpioControllerCount=0;
  assert(AppleEmbeddedGpioControllerLocate(0, &idx, &off, &base) == EFI_NOT_FOUND);
  plat.GpioControllerCount=3;
  ctrls[0].InternalGpioCount=0;
  assert(AppleEmbeddedGpioControllerLocate(0, &idx, &off, &base) == EFI_NOT_FOUND);
  assert(AppleEmbeddedGpioControllerLocate(273, &idx, &off, &base) == EFI_SUCCESS);
  ctrls[0].InternalGpioCount=256;
  reads=writes=0;
  assert(AppleEmbeddedGpioGetGpio(&dummy, 273, NULL) == EFI_INVALID_PARAMETER);
  assert(AppleEmbeddedGpioControllerGetMode(&dummy, 273, NULL) == EFI_INVALID_PARAMETER);
  assert(reads==0 && writes==0);

  /* RMW: non-mask bits (pull/drive/lock/high bits) survive; stray bits in
     Value outside the mask are not written. Old code wrote raw Value and
     zeroed everything outside the field. */
  UINTN mask = GPIOx_REG_DATA | GPIOx_MODE | GPIOx_PERIPHERAL | GPIOx_INPUT_ENABLE;
  mem[(BASE1 >> 2) + 13] = 0xABCD0000u;
  reads = writes = 0;
  /* Stray Value bit 20 is outside the mask and clear in the preset original,
     so it must not appear in the written value. (Bit 31 is preserved from the
     original register by design; that is the whole point of the RMW fix.) */
  AppleEmbeddedGpioControllerSetRegister(BASE1, 13, mask,
      FIELD_PREP(GPIOx_MODE, GPIOx_OUT_MODE) | GPIOx_REG_DATA | (1ull << 20));
  assert(reads == 1 && writes == 1);
  assert(last_write_addr == BASE1 + GPIO_REG(13));
  assert(last_write_val == ((0xABCD0000u & ~mask) |
      ((FIELD_PREP(GPIOx_MODE, GPIOx_OUT_MODE) | GPIOx_REG_DATA) & mask)));
  assert((last_write_val & (1u << 20)) == 0);

  /* Get returns the raw register word; locate must resolve controller 1. */
  assert(AppleEmbeddedGpioGetGpio(&dummy, 273, &val) == EFI_SUCCESS);
  assert(val == mem[(BASE1 >> 2) + 13]);

  /* Error path: unknown pin returns the error, performs no MMIO, and the
     ASSERT is recorded (RELEASE builds previously continued into an MMIO at
     an uninitialized address). */
  reads = writes = 0; assert_calls = 0;
  assert(AppleEmbeddedGpioGetGpio(&dummy, 999, &val) == EFI_NOT_FOUND);
  assert(assert_calls == 1 && reads == 0 && writes == 0);
  assert(AppleEmbeddedGpioSetGpio(&dummy, 999, GPIO_MODE_OUTPUT_1) == EFI_NOT_FOUND);
  assert(assert_calls == 2 && reads == 0 && writes == 0);
  assert(AppleEmbeddedGpioControllerGetMode(&dummy, 999, &mode) == EFI_NOT_FOUND);
  assert(assert_calls == 3 && reads == 0 && writes == 0);

  /* Mode round-trip through the fixed RMW path. */
  assert(AppleEmbeddedGpioSetGpio(&dummy, 273, GPIO_MODE_OUTPUT_1) == EFI_SUCCESS);
  assert(AppleEmbeddedGpioControllerGetMode(&dummy, 273, &mode) == EFI_SUCCESS);
  assert(mode == GPIO_MODE_OUTPUT_1);
  assert(AppleEmbeddedGpioSetGpio(&dummy, 273, GPIO_MODE_OUTPUT_0) == EFI_SUCCESS);
  assert(AppleEmbeddedGpioControllerGetMode(&dummy, 273, &mode) == EFI_SUCCESS);
  assert(mode == GPIO_MODE_OUTPUT_0);
  /* Preserved bits from the earlier preset must still be there. */
  assert((mem[(BASE1 >> 2) + 13] & 0xABCD0000u & ~mask) == (0xABCD0000u & ~mask));

  /* Literal hardware expectations independent of FIELD_PREP/FIELD_GET:
     output-low=0x002, output-high=0x003, input IRQ-off + enable=0x20e.
     Exercise all four pull settings plus both register polarities. */
  const EMBEDDED_GPIO_MODE modes[] = {GPIO_MODE_OUTPUT_0, GPIO_MODE_OUTPUT_1, GPIO_MODE_INPUT};
  const uint32_t fields[] = {0x002, 0x003, 0x20e};
  for (unsigned pull = 0; pull < 4; ++pull) {
    for (unsigned polarity = 0; polarity < 2; ++polarity) {
      for (unsigned m = 0; m < 3; ++m) {
        uint32_t original = ((polarity ? 0xffffffffu : 0xa5a50000u) & ~0x180u) | (pull << 7);
        mem[(BASE1 >> 2) + 13] = original;
        reads = writes = 0;
        assert(AppleEmbeddedGpioSetGpio(&dummy, 273, modes[m]) == EFI_SUCCESS);
        assert(reads == 1 && writes == 1);
        assert(last_write_val == ((original & ~0x26fu) | fields[m]));
        assert((last_write_val & 0x180u) == (pull << 7));
      }
    }
  }
  /* Unsupported mode must not access the register. */
  reads = writes = 0;
  assert(AppleEmbeddedGpioSetGpio(&dummy, 273, (EMBEDDED_GPIO_MODE)99) == EFI_UNSUPPORTED);
  assert(reads == 0 && writes == 0);

  printf("PASS apple gpio locate/rmw/error-path/pull-preservation\n");
  return 0;
}
"""


class AppleGpioTests(unittest.TestCase):
    def test_production_functions(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "test.c"
            binary = Path(tmp) / "test"
            src.write_text(MOCK + FUNCS + MAIN)
            subprocess.run(
                ["clang", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                 "-I", str(SRC_PATH.parent),
                 "-I", str(ROOT / "Silicon/Apple/AppleSiliconPkg/Include"),
                 str(src), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_source_regressions_absent(self):
        locate = extract("EFI_STATUS AppleEmbeddedGpioControllerLocate")
        self.assertNotIn("*ControllerOffset = Gpio % ", locate)
        setreg = extract("STATIC VOID EFIAPI AppleEmbeddedGpioControllerSetRegister")
        self.assertIn("MmioWrite32((RegisterBase + GPIO_REG(Offset)), UpdatedValue)", setreg)
        self.assertEqual(SRC.count("ASSERT(FALSE);\n    return Status;"), 3)


if __name__ == "__main__":
    unittest.main()
