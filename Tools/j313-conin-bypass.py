#!/usr/bin/env python3
"""J313-only connection-isolation experiment, NOT a ten-second watchdog.

First lazy ConIn attempt runs normally (including its real error). Further
attempts return without connecting; FrontPage omits its broad ConnectAll.
No key/protocol fabrication, UI dependency bypass, wait or second StartImage.
Default is check-only. Apply only to a disposable build tree, after menu trace.
"""
import argparse
import hashlib
import importlib.util
from pathlib import Path
import re

SPEC = importlib.util.spec_from_file_location("menu_trace", Path(__file__).with_name("j313-menu-trace.py"))
TRACE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TRACE)
TARGETS = {n: h for n, h in TRACE.TARGETS.items() if not n.endswith("BmBoot.c")}
BLOCK = re.compile(r"^  /[*] J313-CONIN-BYPASS BEGIN (ADD|CONNECTALL) [*]/\r?\n.*?^  /[*] J313-CONIN-BYPASS END [*]/\r?\n", re.M | re.S)


def block(code, kind="ADD"):
    return "  /* J313-CONIN-BYPASS BEGIN " + kind + " */\n" + code + "  /* J313-CONIN-BYPASS END */\n"


def insert(text, anchor, addition, after=False):
    if text.count(anchor) != 1:
        raise ValueError("anchor not unique: " + anchor)
    return text.replace(anchor, anchor + addition if after else addition + anchor, 1)


def transform(name, text):
    if "\r\n" in text:
        return transform(name, text.replace("\r\n", "\n")).replace("\n", "\r\n")
    if name.endswith("BdsEntry.c"):
        prefix, body = text.split("\nBdsDxeOnConnectConInCallBack (", 1)
        body, suffix = body.split("\n}\n", 1)
        body = insert(body, "  EFI_STATUS  Status;\n", block(
            "  STATIC BOOLEAN  J313ConInAttempted = FALSE;\n"
            "\n"
            "  // Set before calling platform code: nested/repeated requests cannot reconnect.\n"
            "  if (J313ConInAttempted) {\n"
            '    DEBUG ((DEBUG_ERROR, "[J313-CONIN-BYPASS] repeat skipped; existing input unchanged\\n"));\n'
            "    return;\n"
            "  }\n"
            "\n"
            "  J313ConInAttempted = TRUE;\n"
            '  DEBUG ((DEBUG_ERROR, "[J313-CONIN-BYPASS] first real attempt begin (not preemptible)\\n"));\n'
        ), after=True)
        body = insert(body, "  Status = EfiBootManagerConnectConsoleVariable (ConIn);\n", block(
            '  DEBUG ((DEBUG_ERROR, "[J313-CONIN-BYPASS] first real attempt returned: %r\\n", Status));\n'
        ), after=True)
        return prefix + "\nBdsDxeOnConnectConInCallBack (" + body + "\n}\n" + suffix
    anchor = "  EfiBootManagerConnectAll ();\n"
    if text.count(anchor) != 1:
        raise ValueError("ConnectAll anchor not unique")
    return text.replace(anchor, block(
        "  // Experiment: keep existing providers; do not reconnect controllers here.\n"
        "  // The original GOP/font/SWM/OSK checks below still apply.\n"
        '  DEBUG ((DEBUG_ERROR, "[J313-CONIN-BYPASS] FrontPage ConnectAll skipped; UI dependencies still required\\n"));\n',
        "CONNECTALL"), 1)


def unpatch(text):
    def restore(match):
        newline = "\r\n" if "\r\n" in match[0] else "\n"
        return "  EfiBootManagerConnectAll ();" + newline if match[1] == "CONNECTALL" else ""
    return BLOCK.sub(restore, text)


def patch_tree(tree, apply=False, device="j313"):
    if device != "j313":
        raise ValueError("experiment is restricted to j313")
    pending = []
    for name, digest in TARGETS.items():
        path = Path(tree) / name
        raw = path.read_bytes()
        current = raw.decode("utf-8")
        source = unpatch(current)
        base = TRACE.BLOCK.sub("", source)
        if hashlib.sha256(base.encode()).hexdigest() != digest:
            raise ValueError("unaudited source: " + name)
        if source not in (base, TRACE.transform(name, base)):
            raise ValueError("noncanonical menu trace: " + name)
        expected = transform(name, source)
        if current not in (source, expected):
            raise ValueError("modified/partial bypass: " + name)
        pending.append((path, raw, expected.encode()))
    if apply:
        if any(path.read_bytes() != raw for path, raw, _ in pending):
            raise ValueError("source changed during validation")
        for path, raw, expected in pending:
            if raw != expected:
                path.write_bytes(expected)
    return "applied/verified isolation experiment (NOT timeout recovery)" if apply else "applicable/verified (check only)"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tree", type=Path, required=True)
    parser.add_argument("--device", required=True, choices=["j313"])
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    try:
        print(patch_tree(args.tree, args.apply, args.device))
    except (OSError, UnicodeError, ValueError) as exc:
        parser.exit(1, str(exc) + "\n")


if __name__ == "__main__":
    main()
