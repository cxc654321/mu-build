# Original-parser rejection baseline

Before modifying hid_keyboard.c/h, compiled the original parser with the small file-reading probe and ran the 182-byte observed binary. Process exit 0 (probe expected HK_UNSUPPORTED); output:

```text
bytes=182 status=2 valid=0 reports=0
```

SHA-256 was independently checked with shasum -a 256:
`5ad48fbaddbae4d5806c4dbc27c842e535e2954cd140e208494cf4f17fbc47c7`.
The baseline probe now prints stream lengths using the extended API; recompiling it is not a replay of the original-parser baseline. Original executable remains ignored in build/baseline-probe.

The binary/hex/header fixtures are device description data observed from the local macOS registry, not implementation code or a GPL driver port. See source.json and SOURCE.md. No actual input/keystroke reports were captured. All decoded payloads in tests are synthetic.
