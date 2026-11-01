# ACPI reference sources

These `.asl` files are **not built**. No `[Sources]` list has ever referenced
them.

The devices they describe — the internal display (`DISP`), the audio complex
(`MCA0`), the microphone array (`AOPA`) and the camera ISP (`ISP0`) — are
emitted as AML from C in `AcpiPlatform.c`, gated on
`NTASI_ENABLE_GPU_ACPI` / `NTASI_ENABLE_{MCA,AOP,ISP}_PUBLICATION` and driven by
the address tables in `Include/Platform/NtasiMediaJ414s.h`. The ASL here is the
readable statement of what that C produces: the `_HID`s, the `_CRS` shapes, the
`_DSD` property names and the published-GSIV choices, with the reasoning and the
provenance for each address.

They lived beside every platform's real tables, so each new machine copied all
1,228 lines of them and rewrote the machine name through the comments —
producing documents that claimed a display had been measured on a machine nobody
owned. Keeping one copy, next to the code it documents, is both smaller and
true.

Written against J414s. If they are ever built, they become a board package's
tables and are named for the machine they were measured on, like
`Platform/J414sBoardPkg`.
