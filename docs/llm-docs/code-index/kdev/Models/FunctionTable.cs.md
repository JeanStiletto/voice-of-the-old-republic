# FunctionTable.cs (165 lines)

Address → function-name resolver built from Lane's Ghidra XML export (~24K functions, 31MB, ~2-3s parse via streaming `XmlReader`). Sorted-array binary search on `Entry.Start`. Used by `AnalyzeDumpCommand` to resolve crash-context addresses to function names. No caching — the tool is interactive (one run per investigation).

## Declarations (in source order)

- L17 — `sealed class FunctionTable`
- L19 — `readonly record struct Entry(uint Start, uint End, string Name)`
- L22 — `uint ImageBase { get; }`
- L23 — `int Count`
- L31 — `FunctionTable LoadFromGhidraXml(string xmlPath)` — streams `<PROGRAM IMAGE_BASE>`, `<FUNCTION ENTRY_POINT NAME>`, and each function's first `<ADDRESS_RANGE END>` child; sorts entries by `Start`
  note: keeps only the FIRST address range per function (the one overlapping the entry point) — Ghidra sometimes splits a function across non-contiguous ranges
- L117 — `Entry? Find(uint address)` — strict containment (`address <= e.End`); used where only true matches count (e.g. the ESP-scan code-shape filter)
- L131 — `Entry? FindPreceding(uint address)` — binary search for the closest entry with `Start <= address`, even across gaps (thunks/padding/interleaved data Ghidra didn't classify)
- L150 — `string Resolve(uint address)` — `"Name+0xN"` for a strict match, `"Name+~0xN"` (note the `~`) for closest-preceding fallback, `"<not in any function>"` only below the lowest mapped function
