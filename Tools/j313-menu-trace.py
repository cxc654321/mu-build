#!/usr/bin/env python3
"""Opt-in, fail-closed menu call diagnostics for pinned MU sources.

Check-only by default. --apply adds DEBUG_ERROR markers, no control-flow changes.
All targets are validated before any write. Never touches Common/MU.
"""
import argparse
import hashlib
from pathlib import Path
import re

TARGETS = {
    "MU_BASECORE/MdeModulePkg/Library/UefiBootManagerLib/BmBoot.c": "1ba803c4ff250fd77d5bf7ae156464ecaf648bb3c8ddb6c4032ddc8d064cb6a4",
    "MU_BASECORE/MdeModulePkg/Universal/BdsDxe/BdsEntry.c": "028df2c03e027922a2bbde03330f8481068986ae30c1420ad5f54669d7606e73",
    "Common/MU_OEM_SAMPLE/OemPkg/FrontPage/FrontPage.c": "a9f24ce41773f36a9e6ab52b1b0a92908e6eea981e9dd0c0823f7a2f31345179",
}
BLOCK = re.compile(r"^[ ]*/[*] J313-MENU-TRACE BEGIN [*]/\r?\n.*?^[ ]*/[*] J313-MENU-TRACE END [*]/\r?\n", re.M | re.S)


def marker(tag, indent="  ", menu=False, status=False):
    code = indent + "/* J313-MENU-TRACE BEGIN */\n"
    if menu:
        code += indent + "if (BmIsBootManagerMenuFilePath (BootOption->FilePath)) {\n"
        indent += "  "
    args = ', Status' if status else ''
    suffix = ': %r' if status else ''
    code += indent + 'DEBUG ((DEBUG_ERROR, "[J313-MENU] ' + tag + suffix + '\\n"' + args + '));\n'
    if menu:
        indent = indent[:-2]
        code += indent + "}\n"
    return code + indent + "/* J313-MENU-TRACE END */\n"


def insert(text, anchor, tag, after=False, indent="  ", menu=False, status=False):
    if text.count(anchor) != 1:
        raise ValueError("anchor not unique: " + tag)
    addition = marker(tag, indent, menu, status)
    return text.replace(anchor, anchor + addition if after else addition + anchor, 1)


def transform(name, text):
    if "\r\n" in text:
        return transform(name, text.replace("\r\n", "\n")).replace("\n", "\r\n")
    if name.endswith("BmBoot.c"):
        prefix, text = text.split("\nEfiBootManagerBoot (", 1)
        text, suffix = text.split("\n}\n", 1)
        anchor = '  OptionNumber = BmFindBootOptionInVariable (BootOption);\n'
        text = insert(text, anchor, 'BOOT-ENTER', menu=True)
        anchor = '    EfiBootManagerConnectDevicePath (BootOption->FilePath, NULL);\n'
        text = insert(text, anchor, 'PATH-CONNECT-BEGIN', indent="    ", menu=True)
        text = insert(text, anchor, 'PATH-CONNECT-END', after=True, indent="    ", menu=True)
        anchor = '    FileBuffer = BmGetNextLoadOptionBuffer (LoadOptionTypeBoot, BootOption->FilePath, &FilePath, &FileSize);\n'
        text = insert(text, anchor, 'BUFFER-BEGIN', indent="    ", menu=True)
        text = insert(text, anchor, 'BUFFER-END', after=True, indent="    ", menu=True)
        text = insert(text, '      Status = gBS->LoadImage (\n', 'LOAD-BEGIN', indent="      ", menu=True)
        text = insert(text, '                      &ImageHandle\n                      );\n', 'LOAD-END', after=True, indent="      ", menu=True, status=True)
        anchor = '  Status = gBS->StartImage (ImageHandle, &BootOption->ExitDataSize, &BootOption->ExitData);\n'
        text = insert(text, anchor, 'START-BEGIN', menu=True)
        text = insert(text, anchor, 'START-END', after=True, menu=True, status=True)
        text = prefix + "\nEfiBootManagerBoot (" + text + "\n}\n" + suffix
    elif name.endswith("BdsEntry.c"):
        anchor = '  PlatformBootManagerOnDemandConInConnect ();                           // MU_CHANGE\n'
        text = insert(text, anchor, 'CONIN-CALLBACK-BEGIN')
        text = insert(text, anchor, 'CONIN-PLATFORM-END', after=True)
        anchor = '  Status = EfiBootManagerConnectConsoleVariable (ConIn);\n'
        text = insert(text, anchor, 'CONIN-CONNECT-END', after=True, status=True)
        anchor = '    DEBUG ((DEBUG_WARN, "[Bds] Connect ConIn failed - %r!!!\\n", Status));\n  }\n'
        text = insert(text, anchor, 'CONIN-CALLBACK-END', after=True, status=True)
    else:
        text = insert(text, '  UINT32      OSKMode = 0;\n', 'FRONTPAGE-ENTER', after=True)
        for anchor, tag, status in [
            ('  EfiBootManagerConnectAll ();\n', 'FP-CONNECT-ALL', False),
            ('  SetGraphicsConsoleMode (GCM_NATIVE_RES);\n', 'FP-GRAPHICS', False),
            ('  Status = InitializeUIToolKit (ImageHandle);\n', 'FP-TOOLKIT', True),
            ('  InitializeFrontPage (TRUE);\n', 'FP-HII', False),
            ('  Status = InitializeFrontPageUI ();\n', 'FP-UI', True),
        ]:
            text = insert(text, anchor, tag + '-BEGIN')
            text = insert(text, anchor, tag + '-END', after=True, status=status)
        text = insert(text, '  Status = mFormBrowser2->SendForm (\n', 'FP-SEND-FORM-BEGIN')
        text = insert(text, '                            &ActionRequest\n                            );\n', 'FP-SEND-FORM-END', after=True, status=True)
    return text


def patch_tree(tree, apply=False):
    pending = []
    for name, digest in TARGETS.items():
        path = Path(tree) / name
        raw = path.read_bytes()
        current = raw.decode('utf-8')
        base = BLOCK.sub('', current)
        if hashlib.sha256(base.encode()).hexdigest() != digest:
            raise ValueError("unaudited source: " + name)
        expected = transform(name, base)
        if current not in (base, expected):
            raise ValueError("modified/partial markers: " + name)
        pending.append((path, raw, expected.encode()))
    if apply:
        # Recheck before writing: reject concurrent edits rather than overwrite them.
        if any(path.read_bytes() != raw for path, raw, _ in pending):
            raise ValueError("source changed during validation")
        for path, raw, expected in pending:
            if raw != expected:
                path.write_bytes(expected)
    return "applied/verified" if apply else "applicable/verified (check only)"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tree', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    try:
        print(patch_tree(args.tree, args.apply))
    except (OSError, UnicodeError, ValueError) as exc:
        parser.exit(1, str(exc) + '\n')


if __name__ == '__main__':
    main()
