#
# J813 auto-run diagnostics. Attempt 40 (FW-11).
#
# Console INPUT does not work yet, so nothing can be typed at the Shell prompt.
# The Shell runs this file automatically, which gives us command execution with
# no input at all -- and its output goes to ConOut, which since FW-7 includes the
# VUART, so all of it lands in the screenlog.
#
# NOTE on echo: the Shell parses a leading '-' as a flag, so `echo ---- foo`
# fails with "Unknown flag". Use '=' or plain words. Never start an echo argument
# with a dash.
#
# CHANGED THIS ATTEMPT:
#   - `connect -r` removed. FW-10 confirmed it hangs at UsbRootHubInit, and it
#     ends the script there. Nothing after it ever ran. It was only ever a USB
#     probe, and the roadmap has USB as explicitly non-blocking, so it buys
#     nothing and costs every command that would follow.
#   - Added the Phase 1.3 test: execute a PE off the RAM disk.
#
@echo -off

echo ==================== J813-STARTUP-NSH BEGIN ====================

echo .
echo ===== map -r : filesystems =====
echo FS0 is the firmware volume. FS1 is the embedded RAM disk (new in FW-10).
map -r

echo .
echo ===== ramdisk contents =====
dir FS1:
dir FS1:\EFI\BOOT

echo .
echo ==================== PHASE 2.1: read-only GPT storage probe ==============
echo Hello World already proved that FS1 can load and relocate an AArch64 PE.
echo This payload enumerates Block I/O handles and reads only LBA 0, LBA 1,
echo and the primary GPT entry array. It never calls WriteBlocks.
echo .
echo Expect: AURORA STORAGE PROBE BEGIN, per-device read status, and GPT data.
echo .

FS1:\EFI\BOOT\BOOTAA64.EFI

echo .
echo ==================== STORAGE PROBE RETURNED ==============================
echo Review SPROBE lines on the VUART for the exact device and LBA result.

echo .
echo ==================== J813-STARTUP-NSH END ====================
