# EngineAddresses.cs (287 lines)

Harvests every hardcoded engine address the patch depends on, for `kdev sigscan`
to rebase. Two sources, with opposite failure modes: `hooks.toml`'s
`[[hooks]] address = 0x...` detour sites (25 of them — fail-safe, since
`KotorPatcher` verifies the bytes and refuses a mismatch) and address literals in
`patches/Accessibility/*.cpp`/`*.h` (NOT fail-safe — a wrong address is
`reinterpret_cast` and called, crashing into an unrelated function).
Comment-stripping (line-based, not a full lexer) keeps prose-quoted addresses out.
Consumed by `SigScanCommand`.

## The C++ side is a literal sweep, not a declaration matcher

Rewritten 2026-07-29 after the old design lost twelve addresses. It used to key
on `^(constexpr|const) uintptr_t NAME = 0x…;`. An address written any other way
was not merely unmatched — it was **invisible**: no signature, no rebase-table
entry, and no appearance in the unresolved list either. The three shapes that
escaped were `static constexpr uintptr_t` (leading `static`), `constexpr
std::uintptr_t` (`std::` prefix) and inline `reinterpret_cast<PFN>(0x…)` (not a
declaration at all). Each stayed at its reference value on the Allard build and
called -272..+464 bytes into an unrelated function.

The matcher could not see `= acc::addr::R(0x…);` either — which is the *correct*
form — so regenerating the table would have silently dropped nearly every
address already in it. The current table survived only because it predated the
R() wrapping.

So the rule is inverted: **every hex literal in the image's VA range counts**
unless a marker opts it out. Declaration shapes are still matched, but only to
recover a readable name for the report; an unnamed hit is reported as
`(inline) <code snippet>`. A new declaration style can no longer hide. The cost
is the occasional false positive, which is one noisy report line — against a
false negative, which is a crash on someone else's machine.

Opt-out markers, matched on the RAW line so they survive comment-stripping:
`kdev-sigscan: ignore` (one line) and `kdev-sigscan: ignore-file` (whole file).
`engine_rebase.cpp`'s `kXrefTable` uses the per-line form, because its
right-hand column holds *target*-build addresses that are meaningless in the
reference image.

## Declarations (in source order)

- `sealed record EngineAddress(uint Va, string Name, string Source, int Line, byte[]? OriginalBytes = null)`
- `static class EngineAddresses`
- `VaLow` / `VaHigh` — the coarse 0x00400000..0x00900000 window for the sweep.
  Deliberately coarse: final section classification (.text vs data vs outside
  image) belongs to `SigScanCommand`, which has the PE.
- `Regex DeclRe` — a named declaration, permissive on everything but the address:
  `[static|inline]* [constexpr|const] [std::]uintptr_t NAME = 0x… | R(0x…)`
- `Regex PendingDeclRe` / `ContinuationRe` — the same with the value on the next line
- `Regex HexLiteralRe` — the catch-all sweep
- `Regex XrefRowRe` — a `{ 0xREF, 0xTARGET },` row in `kXrefTable`
- `List<EngineAddress> Collect(string sourceDir, string repoRoot)` — scans all
  `.cpp`/`.h` under `sourceDir` plus `hooks.toml`, per-line comment-stripped
- `Dictionary<uint,uint> CollectHandResolved(string sourceDir)` — reads
  `engine_rebase.cpp`'s `kXrefTable`. Exists so `sigscan` does not report the
  permanently hand-resolved pair as missing on every run; a warning that is
  always on is a warning nobody reads.
- `bool TryVa(string hex, out uint va)` — parse + range filter; over-long
  literals are rejected rather than throwing
- `string Snippet(string code)` — one-line, 70-char rendering of the code an
  unnamed address came from
- `string StripComments(string line, ref bool inBlockComment)` — blanks `//` and
  `/* */` text; does not track string literals (safe given this codebase's
  statement shapes)
- `List<EngineAddress> ParseHooks(string path, string rel)` — parses `[[hooks]]`
  blocks for `address`, `function`, `original_bytes`
