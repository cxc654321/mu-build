# J313 UI dependency chain (GOP -> SRE -> SWM -> OSK -> THEME -> HII -> FrontPage)

Status: bring-up. This file records what the chain needs, what this platform has,
and how each step is verified on the panel. Update it as things start working.

## 1. The chain

| # | Link | Protocol GUID | Producer module | Module type | Depex | Consumed by |
|---|------|---------------|-----------------|-------------|-------|-------------|
| 0 | Framebuffer / GOP | `gEfiGraphicsOutputProtocolGuid` | `AppleSiliconPkg/Drivers/SimpleFbDxe` | DXE_DRIVER | TRUE | SRE, SWM, FrontPage, GraphicsConsole |
| 1 | Simple Rendering Engine | `gMsSREProtocolGuid` `{7768969c-1d94-4d19-b0cf-9f66cc5934b4}` | `MsGraphicsPkg/RenderingEngineDxe` | DXE_DRIVER | TRUE | SimpleWindowManagerDxe |
| 2 | Simple Window Manager | `gMsSWMProtocolGuid` `{9d400d20-6f35-4268-904f-dc04b1877b62}` | `MsGraphicsPkg/SimpleWindowManagerDxe` | UEFI_DRIVER (no depex) | TRUE | OnScreenKeyboardDxe, FrontPage, BootMenu |
| 3 | On-screen keyboard | `gMsOSKProtocolGuid` `{3c4ca20d-c95a-4b8b-81af-94a9830923e2}` | `MsGraphicsPkg/OnScreenKeyboardDxe` | UEFI_DRIVER | TRUE | FrontPage, SWM |
| 4 | UI theme | `gMsUiThemeProtocolGuid` `{fc111ae5-f073-4348-aa5c-49124bbfed7b}` | `MsGraphicsPkg/MsUiTheme/Dxe/MsUiThemeProtocol` | DXE_DRIVER | TRUE | SWM, FrontPage, DisplayEngine |
| 5 | HII font / database | `gEfiHiiFontProtocolGuid`, `gEfiHiiDatabaseProtocolGuid` | `MdeModulePkg/Universal/HiiDatabaseDxe` | DXE_DRIVER | TRUE | SWM, FrontPage, DisplayEngine |
| 6 | Front page (boot manager menu) | (application) | `OemPkg/FrontPage` | UEFI_APPLICATION | n/a | started by BDS via `PcdBootManagerMenuFile` |
| 7 | Boot menu | (application) | `OemPkg/BootMenu` | UEFI_APPLICATION | n/a | started by BDS |

FrontPage's entry (`OemPkg/FrontPage/FrontPage.c`) locates, in this order, and
fails the whole image on the first miss:

1. `gEfiGraphicsOutputProtocolGuid`
2. `gEfiHiiFontProtocolGuid`
3. `gMsSWMProtocolGuid`
4. `gMsOSKProtocolGuid`

SimpleWindowManagerDxe's `DriverInitStage2()` locates, in this order, and bails
out of publishing `gMsSWMProtocolGuid` on the first miss:

1. `gMsSREProtocolGuid`  ("Failed to find Rendering Engine after finding GOP")
2. `gEfiHiiFontProtocolGuid`
3. custom SWM fonts via HII
4. `gEfiSimpleTextInputExProtocolGuid` on `gST->ConsoleInHandle`

## 2. What this platform actually ships

`Silicon/Apple/AppleSiliconPkg/FrontpageDsc.inc` + `FrontpageFdf.inc` decide the
UI set. Both had UI pieces commented out that the chain above needs.

| Module | In DSC | In FDF | Notes |
|--------|--------|--------|-------|
| RenderingEngineDxe | yes (uncommented in this bring-up) | yes (uncommented) | was `#` in both; SWM cannot publish without it |
| SimpleWindowManagerDxe | yes | yes | no `[Depex]` section -> TRUE |
| OnScreenKeyboardDxe | yes | yes | moved before FrontPage |
| MsUiTheme/Dxe | yes | yes | |
| DisplayEngineDxe | yes | yes | |
| FrontPage | yes | yes | runs (observed `UI: FP entry`) |
| GopOverrideDxe | **commented out** | **commented out** | optional; SRE provides its own GOP |

Dispatch order for depex-TRUE images is FV order, and `FrontpageFdf.inc` is
appended after `AppleSiliconFvMain.fdf.inc`, so these UI modules are dispatched
after the core drivers but before BDS loads applications. FrontPage and BootMenu
are applications, so they are started by BDS long after all of this.

## 3. How J813/J414s compare

They use the same shared include, so the *list* is identical; only the platform
DEFINEs differ (`PLATFORM_ENABLE_MTP_HID`, `PLATFORM_ENABLE_COLORBARS`, ...).
J813/J414s therefore have the same commented-out RenderingEngineDxe; if their
FrontPage works, either they do not use the OEM front page at all, or SRE comes
from somewhere else in their FV. Verified difference on J313: nothing in the
chain provides `gMsSREProtocolGuid` unless RenderingEngineDxe is packaged.

## 4. Verification strategy

"No per-module guesswork": at `ReadyToBoot` the `J313MemMapDxe` driver walks the
DXE debug image table and prints every loaded image by name (the core maintains
this table, so it cannot be wrong the way a hand-inserted DEBUG can):

```
LOADED: <module>.PDB base 0x... size 0x...
LOADED: N images
```

If `RENDERINGENGINEDXE.PDB` is absent from that list, the module was never
dispatched, whatever its sources say. If it is present and `SIMPLEWINDOWMANAGERDXE.PDB`
is not, SRE's producer ran but SWM still bailed out, and SWM's own
`ERROR [SWM]: ...` line names the missing dependency.

## 5. Phases and acceptance

| Phase | Change | Accept when the panel shows |
|-------|--------|------------------------------|
| P0 | instrument only (`Tools/ui-trace.py`), `LOADED:` dump | `LOADED:` list; presence/absence of each UI module |
| P1 | SRE packaged (done) | `RENDERINGENGINEDXE.PDB` in `LOADED:`; no `Failed to find Rendering Engine` |
| P2 | SWM publishes | `SIMPLEWINDOWMANAGERDXE.PDB` listed; `ERROR [SWM]` gone; OSK's "Failed to find SWM" gone |
| P3 | OSK publishes | `ONSCREENKEYBOARDDXE.PDB` listed; no `ERROR [OSK]` |
| P4 | HII font + theme | no `Failed to find Font protocol` from FP/SWM |
| P5 | FrontPage runs | `UI: FP entry`, then the front page draws instead of `DeviceBootManagerUnableToBoot` |
| P6 | input | key handling, or at least a cursor; OSK is the fallback given no MTP HID |

Each phase is one commit and one flash. The acceptance line is what makes the
phase pass; if it does not appear, the phase is not done.
