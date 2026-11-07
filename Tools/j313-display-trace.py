#!/usr/bin/env python3
"""Pinned display evidence; apply only to disposable build trees."""
import argparse
import hashlib
from pathlib import Path
import re
TARGET = "Common/MU/MsGraphicsPkg/DisplayEngineDxe/FormDisplay.c"
DIGEST = "906e54828e084f7ee268ef85b64900ad645359f3925acee724d3448b78e5f593"
BLOCK = re.compile(r"^[ ]*/[*] J313-DISPLAY BEGIN [*]/\r?\n.*?^[ ]*/[*] J313-DISPLAY END [*]/\r?\n", re.M | re.S)

def block(code, indent="  "):
    return indent + "/* J313-DISPLAY BEGIN */\n" + code + indent + "/* J313-DISPLAY END */\n"

def transform(text):
    if "\r\n" in text:
        return transform(text.replace("\r\n", "\n")).replace("\n", "\r\n")
    def insert(anchor, addition, after=False):
        nonlocal text
        if text.count(anchor) != 1:
            raise ValueError("nonunique anchor: " + anchor)
        text = text.replace(anchor, anchor + addition if after else addition + anchor, 1)
    def log(tag, indent="  "):
        return block(indent + 'DEBUG ((DEBUG_ERROR, "[J313-STEP] ' + tag + '\\n"));\n', indent)
    insert("  ZeroMem (&InputState, sizeof (InputState));\n", block("  STATIC BOOLEAN  J313FirstWait = TRUE;\n"))
    insert("      mGop->Blt (\n", log("DE-FIRST-FILL-BEGIN", "      "))
    insert("    } else {\n      mPrivateData.PreviousCanvas->ClearCanvas", log("DE-FIRST-FILL-END", "      "))
    insert("  Status = CreateFormControls (\n", log("DE-CONTROLS-BEGIN"))
    insert("             &FormCanvas\n             );\n", log("DE-CONTROLS-END"), True)
    insert("    LocalCanvas->Base.Draw (\n", log("DE-DRAW-BEGIN", "    "))
    insert("    mControlsRequireRefresh = FALSE;\n    mRefreshOnEvent         = FALSE;\n", log("DE-DRAW-END", "    "))
    insert("    Status          = mSWMProtocol->WaitForEvent (EventNum, WaitEvents, &i, Timeout, mRefreshOnEvent);\n", block('    if (J313FirstWait) {\n      DEBUG ((DEBUG_ERROR, "[J313-STEP] DE-FIRST-WAIT-BEGIN\\n"));\n    }\n', "    "))
    insert("    Status          = mSWMProtocol->WaitForEvent (EventNum, WaitEvents, &i, Timeout, mRefreshOnEvent);\n", block('    if (J313FirstWait) {\n      J313FirstWait = FALSE;\n      DEBUG ((DEBUG_ERROR, "[J313-STEP] DE-FIRST-WAIT-END: %r\\n", Status));\n    }\n', "    "), True)
    return text

def patch_tree(tree, apply=False):
    path = Path(tree) / TARGET
    raw = path.read_bytes()
    current = raw.decode()
    base = BLOCK.sub("", current)
    if hashlib.sha256(base.encode()).hexdigest() != DIGEST:
        raise ValueError("unaudited source: " + TARGET)
    expected = transform(base)
    if current not in (base, expected):
        raise ValueError("modified/partial display markers")
    if apply:
        if path.read_bytes() != raw:
            raise ValueError("source changed during validation")
        if current != expected:
            path.write_bytes(expected.encode())
    return "applied/verified" if apply else "applicable/verified (check only)"

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tree", required=True, type=Path)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    try:
        print(patch_tree(args.tree, args.apply))
    except (ValueError, OSError) as error:
        parser.exit(1, str(error) + "\n")
