#!/usr/bin/env python3
# Instrument the Mu UI chain so one boot says which link is missing.
#
# GOP -> SRE -> SWM -> OSK -> THEME -> FrontPage. Entry prints say whether a
# driver ran at all; install prints sit immediately before each protocol
# publication. Applied in CI only: Common/MU and Common/MU_OEM_SAMPLE are
# submodules, so the patch cannot live in this repository as source.

import os

ROOT = 'Common/MU/MsGraphicsPkg/'
OEM = 'Common/MU_OEM_SAMPLE/OemPkg/'

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
    (ROOT + 'SimpleWindowManagerDxe/SimpleWindowManagerProtocol.c', 'SWM'),
    (ROOT + 'OnScreenKeyboardDxe/OnScreenKeyboardProtocol.c', 'OSK'),
    (ROOT + 'MsUiTheme/Dxe/MsUiThemeProtocol.c', 'THEME'),
]

NEWLINE = chr(92) + 'n'
QUOTE = chr(34)


def debug_line(tag, what):
    prefix = '  DEBUG ((DEBUG_ERROR, ' + QUOTE + 'UI: ' + tag + ' ' + what
    return prefix + NEWLINE + QUOTE + '));'


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
            lines.insert(j + 1, debug_line(tag, 'entry'))
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
            out.append(debug_line(tag, 'install call'))
            done = True
        out.append(line)
    if done:
        open(path, 'w').write(chr(10).join(out))
    return done


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


if __name__ == '__main__':
    main()
