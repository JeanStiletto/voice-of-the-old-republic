# PriorityGroup2da.cs (267 lines)

Static byte-level editor for `prioritygroups.2da` (binary V2.b format) that appends the mod's two dedicated audio priority-group rows without disturbing any existing vanilla row. Hand-rolls the 2DA V2.b parser/builder (magic, column header, row labels, uint16 cell-offset table, NUL-terminated data block) rather than depending on a general 2DA library, because it only ever appends at the tail (existing offsets never move). Read at runtime by the accessibility DLL, which locates the injected rows not by index (which could drift if another mod also extends the table) but by a sentinel value stamped in the `FadeTime` column.

Two rows: a flat full-volume group (`SentinelFadeTime` 31337, 10 m/20 m falloff, for on-demand cues like cycling/beacon/combat) and a near-field spatial group (`SpatialSentinelFadeTime` 31338, tight 1 m/8 m falloff, for passive proximity cues via `PlayCueAtPosition`). `AppendAccGroup` is idempotent — adds only missing rows, returns the input unchanged (reference-equal) if both already present, and self-checks the output before returning.

## Declarations (in source order)

- L40 — `public static class PriorityGroup2da`
- L43 — `public const ushort SentinelFadeTime = 31337` — flat full-volume group fingerprint
- L46 — `public const ushort SpatialSentinelFadeTime = 31338` — near-field spatial group fingerprint
- L56 — `private static readonly Dictionary<string,string> FlatRow` — clone of vanilla group 0 but Volume=127, Interrupt=1, 10m/20m falloff
- L80 — `private static readonly Dictionary<string,string> SpatialRow` — Volume=127, 1m/8m tight falloff band
- L94 — `private static readonly (ushort Sentinel, Dictionary<string,string> Row)[] AccRows` — the two rows to maintain
- L103 — `private sealed class Table` — in-memory parsed representation (Cols, RowCount, Labels, Offsets, DataBlock)
- L112 — `private static Table Parse(byte[] buf)`
  note: throws `InvalidDataException` on bad magic, unterminated header/labels, or data block overrun
- L156 — `private static byte[] Build(Table t)` — serializes a `Table` back to 2DA V2.b bytes
- L176 — `private static string Cell(Table t, int row, int colIdx)`
- L183 — `private static int FadeTimeCol(Table t)`
- L187 — `private static bool HasSentinel(Table t, int fadeCol, ushort sentinel)`
- L197 — `public static bool HasAccGroup(byte[] buf)` — true only if ALL sentinel rows present
- L208 — `private static void AppendRow(Table t, Dictionary<string,string> row)`
  note: "label" column always written empty (shared offset 0 into data block)
- L243 — `public static byte[] AppendAccGroup(byte[] source)`
  note: returns `source` unchanged (by reference) when nothing to add; self-checks via `HasAccGroup` on the built output before returning, throws if the check fails
