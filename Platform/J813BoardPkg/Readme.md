# J813BoardPkg

The ACPI describing the logic board of the MacBook Air (M5, 2026), measured on
that machine.

See `../J414sBoardPkg/Readme.md` for what a board package is and why it is named
for a machine rather than a SoC.

## Machines that share it

Only j813. J704 (MacBook Pro, M5, 2025) is the same silicon but not the same
board — its DSDT differs by 71 lines of code and it has neither an MTP nor an
SMCG device — so it keeps its own tables in `MacBookProLate2025Pkg`.
