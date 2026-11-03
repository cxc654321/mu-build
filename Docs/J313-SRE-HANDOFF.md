# J313 synchronous GOP → SRE handoff

## Audit and decision (before implementation)

Baseline: e5e03e7; MU submodule fa4cfd83. SimpleFbDxe publishes a standard GOP with device path/EDID. RenderingEngine entry only installs DriverBinding; Supported/Start consume PcdMsGopOverrideProtocolGuid BY_DRIVER. Start publishes GOP + SRE on the *same* controller.

First evaluated enabling upstream GopOverrideDxe in APRIORI followed by explicit connection. Rejected unchanged: it installs override before removing GOP, leaves override on uninstall failure, registers a late GOP notification when discovery fails, and cannot roll back a failed SRE start. Merely changing FV order does not bind SRE. Late conversion after ConSplitter/GraphicsConsole can encounter consumers or multiple GOPs.

Use a small J313-only orchestration driver, not a second renderer or copied upstream driver. Build switch `J313_SRE_HANDOFF` defaults TRUE only in MacBookAirMid2020 DSC (command-line FALSE disables it). APRIORI: SimpleFb → existing RenderingEngine entry → J313 handoff → existing console drivers. No other platform enables it. Existing shared Frontpage includes and SimpleFb remain unchanged.

The handoff runs synchronously at DXE entry, not from protocol notifications. It first resolves exactly one GOP and the RenderingEngine DriverBinding by its loaded-image FV file GUID (02AA389E-CA29-43BF-BE05-A4CA038812F2). Missing/ambiguous prerequisites leave GOP untouched. Remove the original GOP before publishing override, then explicitly invoke that binding Supported and Start (not ConnectAll, nor a priority list which could also run other drivers). Verify both the replacement GOP and SRE on this same handle. Only after return may console drivers dispatch. Thus the early SimpleFb GOP exists until the bounded transaction; no persistent override-only state and no duplicate standard GOP installation.

## Rollback and limits

No retry event is installed. On override install/Supported/Start/verification failure: if SRE published protocols, call its Stop before removing override; otherwise close only this binding's BY_DRIVER open (Start can fail before installing protocols). Remove override, then restore the saved SimpleFb GOP on the same handle. Log every failure, including rollback failures; never install the original over a live replacement. If Stop fails, retain the wrapper/override rather than invalidate the live wrapper's parent. If original GOP uninstall fails, return without publishing override. Restoration can itself fail (e.g. firmware allocation failure); report this as a hard failure, not successful fallback.

Upstream SRE has imperfect failed-start/Stop cleanup: Stop cancels but does not close its timer event, and a pre-publication failed Start retains global state. This driver never retries that SRE instance; it does not modify upstream rendering code for other platforms. This is a boot-time handoff, not a general hotplug/unload implementation.

## Executed validation

- `python3 Tests/test_j313_sre_handoff.py`: 3 tests PASS; actual C compiled with `cc -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter`, then 15 success/fault scenarios PASS. The fake models protocol ownership/publication, not the full DXE dispatcher or upstream SRE timer implementation.
- `clang -target aarch64-unknown-windows -fshort-wchar -fsyntax-only -Werror -Wno-unused-parameter -D_PCD_GET_MODE_PTR_PcdMsGopOverrideProtocolGuid="((void*)0)" -I ../mu-build-tree/MU_BASECORE/MdePkg/Include -I ../mu-build-tree/MU_BASECORE/MdePkg/Include/AArch64 -I ../mu-build-tree/Common/MU/MsGraphicsPkg/Include Silicon/Apple/AppleSiliconPkg/Drivers/J313SreHandoffDxe/J313SreHandoffDxe.c`: PASS against existing real headers. Initial attempt caught missing UefiLib/BaseMemoryLib declarations, corrected along with INF libraries; final command exits 0. PCD macro is a syntax-only stub, not a firmware build.
- `git diff --check`: PASS.
- Full linked firmware / GenFds build and hardware boot NOT run; isolated worktree has uninitialized submodules. Main-tree headers were only read. Parent must run normal build for both switch values and inspect generated APRIORI before boot.
- If override removal itself fails, the original GOP restore is still attempted so the console can survive, but a stale override may remain. This is explicitly an unrecovered cleanup failure, not a successful transaction; do not infer safe later reconnect from that injected case.

## Validation plan

Compile/run the actual orchestration C against a host fake Boot Services/DriverBinding layer: success ordering, missing/multiple GOP or binding, original uninstall failure, override install failure, Supported failure, Start failure before/after protocol publication, verification failure, Stop failure, override removal failure, original restore failure. Check standard GOP is absent at Start and present on all recoverable exits, no premature mutation on prerequisite failures, and no generic ConnectController call. Static checks ensure J313-only DSC/FDF opt-in and APRIORI ordering, and upstream override remains disabled. Run git diff --check. Firmware build if existing tooling permits; host tests are not a firmware/hardware result.

Hardware acceptance (not performed here): build both switch values, boot RAM payload without writing ESP; capture serial logs showing handoff commit and same-handle GOP/SRE, then SWM publication and FrontPage startup; verify console fallback with a deliberately unavailable SRE. Confirm native framebuffer mode; scaled-mode checksum correctness and SWM/input bring-up are separate issues. No claim of working J313 UI without these observations.
