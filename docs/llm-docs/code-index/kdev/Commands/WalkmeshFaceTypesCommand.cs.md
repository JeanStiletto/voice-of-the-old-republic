# WalkmeshFaceTypesCommand.cs (151 lines)

`kdev walkmesh-facetypes` — histograms `face_type` (surfacemat.2da row id) values in a KOTOR `.wok` walkmesh: single-file mode (positional `path` arg) or `--dir` directory-aggregation mode with a `MaterialNames` label lookup. `--all` (dir mode only) includes placeable/creature/door walkmeshes, not just area rooms (filename `mNN...`). Parses the raw BWM v1.0 binary format directly (magic check, `face_type` array at header offset 0x58). No dependencies on other kdev classes.

## Declarations (in source order)

- L16 — `static class WalkmeshFaceTypesCommand`
- L19 — `string[] MaterialNames` — surfacemat.2da row-id → label (0=NotDefined .. 21=Trigger)
- L45 — `Command Build()` — `path` positional (optional), `--dir`, `--all`
- L72 — `int Run(string path)` — single-file histogram
- L99 — `int RunDir(string dir, bool includeAll)` — aggregated histogram across matching `.wok` files, sorted by face count descending
- L145 — `string LabelFor(int id)`
- L148 — `uint ReadU32(byte[] b, int off)`
