#!/usr/bin/env python3
# Instrument the Mu UI chain so one boot says which link is missing.
#
# Applied in CI only: Common/MU and Common/MU_OEM_SAMPLE are submodules, so the
# patch cannot live in this repository as source.

import os

ROOT = 'Common/MU/MsGraphicsPkg/'
OEM = 'Common/MU_OEM_SAMPLE/OemPkg/'

QUOTE = chr(34)
BS = chr(92)
NL = BS + 'n'


def dbg(tag_text):
    return '  DEBUG ((DEBUG_ERROR, ' + QUOTE + tag_text + NL + QUOTE + '));'


ENTRIES = [
    (ROOT + 'RenderingEngineDxe/RenderingEngine.c', 'DriverInit', 'SRE'),
    (ROOT + 'SimpleWindowManagerDxe/WindowManager.c', 'DriverInit', 'SWM'),
    (ROOT + 'OnScreenKeyboardDxe/OnScreenKeyboardDriver.c', 'OSKDriverEntryPoint', 'OSK'),
    (ROOT + 'MsUiTheme/Dxe/MsUiThemeProtocol.c', 'MsUiThemeProtocolEntry', 'THEME'),
    (ROOT + 'DisplayEngineDxe/FormDisplay.c', 'InitializeDisplayEngine', 'DE'),
    (OEM + 'FrontPage/FrontPage.c', 'UefiMain', 'FP'),
]

INSTALLS = [
    (ROOT + 'RenderingEngineDxe/RenderingEngine.c', 'SRE'),
    (ROOT + 'SimpleWindowManagerDxe/WindowManager.c', 'SWM'),
    (ROOT + 'MsUiTheme/Dxe/MsUiThemeProtocol.c', 'THEME'),
]

WM = ROOT + 'SimpleWindowManagerDxe/WindowManager.c'
OSK_DRV = ROOT + 'OnScreenKeyboardDxe/OnScreenKeyboardDriver.c'

ANCHORS = [
    (WM, 'mTheme = MsUiGetPlatformTheme ();', 'after', 'SWMTrace: entry'),
    (WM, '// Graphics Output Protocol is available now, complete driver initialization.', 'before', 'SWMTrace: GOP present on console handle'),
    (WM, "// Graphics Output Protocol isn't availble now. Register for Graphics Output Protocol registration notifications.", 'before', 'SWMTrace: no GOP yet, registering notify'),
    (WM, 'Status = DriverInitStage2 (ImageHandle);', 'before', 'SWMTrace: stage2 from entry'),
    (WM, '// If we already found the Graphics Output Protocol we want, there is nothing to do.', 'before', 'SWMTrace: gop callback fired'),
    (WM, 'Status = DriverInitStage2 (mImageHandle);', 'before', 'SWMTrace: stage2 from gop callback'),
    (WM, 'Status = gBS->InstallMultipleProtocolInterfaces (', 'before', 'SWMTrace: installing SWM protocol'),
    (WM, '// Create a periodic timer event for checking the Absolute Pointer protocol watchlist.', 'before', 'SWMTrace: SWM install call returned'),
    # OSK's failure line sits inside a multi-line DEBUG() call, so inserting a
    # statement before it breaks the macro invocation. Entry print is enough.

]


#
# The fix this instrumentation exists to test.
#
# DriverInit() either finds GOP on the console handle (then stage two runs
# immediately) or registers a protocol notify for GOP and returns. The callback
# then locates GOP *and* SRE and bails out silently if SRE is missing. SRE is
# published by RenderingEngineDxe, which installs GOP and SRE one after the
# other: the notify fires on the GOP install, at which point SRE is not there
# yet, the callback does "mGop = NULL; goto Exit;", and no further GOP install
# ever happens -- so gMsSWMProtocol is never published and FrontPage reports
# "Failed to find the Window Manager Protocol".
#
# Registering the same event for SRE as well makes the callback run once more
# when SRE appears, which is the moment it can actually succeed.
#
NOTIFY_OLD = (
    'Status = gBS->RegisterProtocolNotify (' + chr(10) +
    '                  &gEfiGraphicsOutputProtocolGuid,' + chr(10) +
    '                  mGopRegisterEvent,' + chr(10) +
    '                  &mGopRegistration' + chr(10) +
    '                  );'
)

NOTIFY_NEW = NOTIFY_OLD + chr(10) + chr(10) + dbg('SWMTrace: also notifying on SRE install') + chr(10) + (
    '  gBS->RegisterProtocolNotify (&gMsSREProtocolGuid, mGopRegisterEvent, &mGopRegistration);'
)


def patch_notify(path):
    if not os.path.exists(path):
        return False
    src = open(path).read()
    if NOTIFY_OLD not in src:
        return False
    src = src.replace(NOTIFY_OLD, NOTIFY_NEW, 1)
    open(path, 'w').write(src)
    return True


def patch_entry(path, func, tag):
    lines = open(path).read().split(chr(10))
    for i, line in enumerate(lines):
        s = line.strip()
        if s.startswith(func + ' (') or s.startswith(func + '('):
            j = i
            while j < len(lines) and '{' not in lines[j]:
                j += 1
            if j >= len(lines):
                return False
            lines.insert(j + 1, dbg('UI: ' + tag + ' entry'))
            open(path, 'w').write(chr(10).join(lines))
            return True
    return False


def patch_install(path, tag):
    lines = open(path).read().split(chr(10))
    out = []
    done = False
    for line in lines:
        if (not done) and ('gBS->' in line) and ('InstallProtocolInterface (' in line
                                                 or 'InstallMultipleProtocolInterfaces (' in line):
            out.append(dbg('UI: ' + tag + ' install call'))
            done = True
        out.append(line)
    if done:
        open(path, 'w').write(chr(10).join(out))
    return done


def patch_anchor(path, anchor, where, text):
    if not os.path.exists(path):
        return False
    src = open(path).read()
    if anchor not in src:
        return False
    payload = dbg(text)
    if where == 'after':
        src = src.replace(anchor, anchor + chr(10) + payload, 1)
    else:
        src = src.replace(anchor, payload + chr(10) + anchor, 1)
    open(path, 'w').write(src)
    return True


def main():
    for path, func, tag in ENTRIES:
        if not os.path.exists(path):
            print('UI trace: missing ' + path)
            continue
        ok = patch_entry(path, func, tag)
        print('UI trace: entry ' + tag + (' ok' if ok else ' NOT patched'))
    for path, tag in INSTALLS:
        if not os.path.exists(path):
            print('UI trace: missing ' + path)
            continue
        ok = patch_install(path, tag)
        print('UI trace: install ' + tag + (' ok' if ok else ' NOT patched'))
    ok = patch_notify(WM)
    print('UI trace: SRE notify patch ' + ('ok' if ok else 'NOT patched'))
    for path, anchor, where, text in ANCHORS:
        ok = patch_anchor(path, anchor, where, text)
        print('UI trace: anchor ' + text + (' ok' if ok else ' NOT patched'))


if __name__ == '__main__':
    main()
