#
# J704 auto-run diagnostics. Attempt 40 (FW-11).
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

echo ==================== J704-STARTUP-NSH BEGIN ====================

echo .
echo ===== map -r : filesystems =====
echo FS0 is the firmware volume. FS1 is the embedded RAM disk (new in FW-10).
map -r

echo .
echo ===== ramdisk contents =====
dir FS1:
dir FS1:\EFI\BOOT

echo .
echo ==================== PHASE 1.3: execute a PE from FS1 ====================
echo FW-10 proved FS1 mounts and files are readable. That only exercises
echo DiskIo/Partition/Fat. This additionally loads and relocates a PE image from
echo the RAM disk, which is what bootaa64.efi will need to do.
echo .
echo Expect: UEFI Hello World! then a return to this script.
echo .

FS1:\EFI\BOOT\BOOTAA64.EFI

echo .
echo ==================== PE EXECUTION RETURNED ====================
echo If the Hello World line appeared above, Phase 1 is complete: this machine can
echo boot an arbitrary AArch64 UEFI application from a non-firmware volume.

echo .
echo ==================== J704-STARTUP-NSH END ====================
