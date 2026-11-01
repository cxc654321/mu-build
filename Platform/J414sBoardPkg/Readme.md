# J414sBoardPkg

The ACPI describing the logic board of the MacBook Pro (14-inch, M2 Pro, 2023),
measured on that machine.

## Why it is named for a machine

A board package holds the tables that name soldered devices and the addresses,
GPIO numbers and interrupt lines they answer on. Those come from reading a live
machine's ADT and correlating it against the alias table the AIC HAL extension
uses. The comments in these files say where each number came from — a dated ADT
capture with its sha256, a verification script, a bring-up document. That is
what makes them reviewable.

Naming the package for the SoC would have implied it describes T602X in
general. It does not. It describes J414s' board, and the machines below reuse it
because they share that board.

## Machines that share it

| machine | | why |
|---|---|---|
| j414s | MacBook Pro 14" M2 Pro | measured here |
| j416s | MacBook Pro 16" M2 Pro | same board |
| j414c | MacBook Pro 14" M2 Max | same board |
| j416c | MacBook Pro 16" M2 Max | same board |
| j474s | Mac mini M2 Pro | same board |
| j475c | Mac Studio M2 Max | same board |
| j475d | Mac Studio M2 Ultra | same board |
| j180d | Mac Pro M2 Ultra | same board |

That reuse is not an assumption. Each of those machines previously carried its
own copy of these tables, and comparing them with comments stripped showed the
ACPI code to be byte-identical across all eight — the copies differed only in
the machine name, which had been rewritten through the provenance comments so
that they claimed measurements and cited scripts (`tools/verify-j416s-av-adt.py`,
`j474s-adt.bin`) that never existed. Sharing one package keeps the addresses,
which were always right, and drops the false claims about where they came from.

A machine that turns out to differ stops sharing and gets its own package. That
is what `MacBookProLate2025Pkg` already is for J704, whose DSDT genuinely
differs from J813's.

## What is not here

`DISP.asl` and `Media/{MCA,AOPA,ISP}.asl` used to sit beside these files. No
build ever referenced them: those devices are emitted from C in
`AcpiPlatformDxe` under `NTASI_ENABLE_*_PUBLICATION`, and the ASL was a
description of what that C does. It now lives once, next to the C, instead of
being copied beside every board.

`FADT.aslc` is also gone. It belongs to the chassis tier, which already emits
it; carrying it here as well installed the table twice.
