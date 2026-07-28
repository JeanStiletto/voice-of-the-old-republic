# CombatStringsExtractCommand.cs (877 lines)

`kdev combat-strings-extract` — extracts per-locale combat-log parse anchors from a `dialog.tlk` (game install, `--tlk` path, or `--lang` cached copy under `data/dialog-tlk/`) and emits a C++ `MsgStrings` table snippet (`kEn`/`kFr`/etc.) ready to paste into `combat_strings.cpp`. Exploits that the Steam exe is locale-shared, so every combat anchor lives at the same numeric strref across locales (`StrrefMap`, hand-mapped from DE once). Engine anchors (hit/miss phrases, stat templates, save/damage/kill markers) are reconstructed via byte-level template slicing (`TrimAt`, `Between`, `BetweenPlaceholders`, `ReconstructHitMissPhrase`, `SaveMarker`, etc.); speech-side labels are emitted as DE placeholders marked TODO for manual translation. Includes a minimal standalone `TlkFile` reader (12-byte header + 40-byte string-data table) at file scope. Self-contained — no other kdev classes referenced besides `KdevConfig`.

## Declarations (in source order)

- L62 — `static class CombatStringsExtractCommand`
- L64 — `Command Build()` — `--tlk` / `--lang` (mutually exclusive) / `--output`
- L94 — `int Run(string? tlkPath, string? langCode, string? outPath)` — resolves TLK source, loads it, emits snippet to stdout or file
- L173 — `static class StrrefMap` — hardcoded DE dialog.tlk strref constants for every combat/effect/save template (e.g. `SummaryTemplate=42042`, `AbsorbReduction=1455`, `SaveLineTemplate=1406`)
  note: discovered by grepping the DE TLK block 42030-42395 plus a 2026-06-09 pass for the effect/save/damage/kill set; strrefs are locale-stable, only the text differs
- L235 — `record FieldRule(string Name, string Comment, Func<TlkFile, byte[]?> Extract)`
- L237 — `EngineAnchors` — ordered array of `FieldRule` matching `combat_strings.h` field declaration order
- L285 — `SpeechLabels` — DE placeholder speech-side fields (verb_hit, word_critical, ...) needing manual translation
- L299 — `ShortReplacements` — DE placeholder short-form labels
- L315 — `record TrailingField(string Name, string Comment, Func<TlkFile,byte[]?>? Engine, string? SpeechDe)`
- L317 — `TrailingFields` — results-only labels interleaved with effect/save/damage/kill engine anchors
- L348-548 — extraction primitives: `TrimAt`, `TrimAfter`, `LastWordBeforePlaceholder`, `TrimLeadingSpaces`, `StripLeading`, `StripPlaceholders`, `AppendByte`, `SuffixLiteral`, `Between`, `ReconstructHitMissPhrase` — all operate on raw CP-1252 byte arrays to preserve byte fidelity
- L550 — `int IndexOfAscii(byte[] hay, string needle, int from)`
- L564 — `bool StartsWithAscii(byte[] hay, string needle)`
- L579 — `(int,int)? FindPlaceholder(byte[] s, int n)` — tolerant of `< CUSTOMn>` with internal space (FR/IT normalisation)
- L601 — `byte[]? BetweenPlaceholders(byte[]? s, int n0, int n1)`
- L615 — `byte[]? WordBeforePlaceholder(byte[]? s, int n)`
- L630 — `byte[]? TrimSpaces(byte[]? s)`
- L641 — `byte[]? Concat(byte[]? a, byte[]? b)`
- L650 — `byte[] CommonSuffix(byte[] a, byte[] b, byte[] c)`
- L668 — `byte[]? SaveMarker(TlkFile t)` — longest common suffix of the 3 save-type strings + template separator
- L681 — `byte[]? SaveResultPunct(TlkFile t)`
- L698 — `string CppLiteral(byte[]? bytes)` — CP-1252 bytes ≥0x80 as `\xNN`; splits adjacent hex escapes with `""` to dodge C++'s greedy hex-escape rule
- L743 — `string EmitSnippet(TlkFile tlk, string langCode, string tlkPath)` — full snippet + wiring checklist
- L805 — `byte[] EncodeAscii(string s)` — CP-1252 encode of DE placeholder text
- L813 — `string LanguageIdToCode(int languageId)`
- L830 — `sealed class TlkFile` (file-scope, no namespace member of the command) — minimal TLK reader: `LanguageId`, `StringCount`, `Load(path)`, `Get(strref)` returns raw bytes
