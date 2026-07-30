# Phase 3 scan — kdev dev CLI commands

Scope: `tools/kdev/Program.cs` (48), `tools/kdev/Models/FunctionTable.cs` (164),
and `tools/kdev/Commands/`: BuildCommand.cs (627), CombatStringsExtractCommand.cs
(823), AnalyzeDumpCommand.cs (676), SigScanCommand.cs (583), ReCommand.cs (519),
WalkmeshStatsCommand.cs (476), SoundScoreCommand.cs (472), WalkmeshGeometryAuditCommand.cs
(399), DumpTextCommand.cs (307), ApplyCommand.cs (224), WalkmeshFaceTypesCommand.cs
(150), LaunchCommand.cs (114), LogsCommand.cs (88), CleanCommand.cs (62),
StripDumpCommand.cs (57), StatusCommand.cs (55), DevCommand.cs (49),
KillCommand.cs (28). 18 files total, ~5900 lines.

Note per the task brief: this batch is C#, not the C++ patch, so the brief's
`acc::`-namespace / `strings::Get(Id)` / SEH / engine-offset conventions do not
apply here. The METHOD, evidence standard, two-section structure, no-tables
rule and findings-not-fixes rule still bind. `tools/` is gitignored (no
version control, no rollback path) — every finding below is rated with that
extra risk in mind, and nothing here needs it: everything proposed is either
a compiler-checked literal-to-constant swap or a single dead-guard deletion.

Method: full read of all 18 files (no `bin/`/`obj/`). Before flagging anything
"unused" or "already covered elsewhere" I grepped rather than assumed:
- `TryPin` repo-wide (`tools/kdev`) to confirm its only call site before
  proposing to remove its self-guard.
- `FunctionTable.` / `FindPreceding` / `.Resolve(` repo-wide to confirm
  `FunctionTable`'s public surface is exercised (it is, via `AnalyzeDumpCommand`),
  not dead.
- `OffType|OffVertexCount|WalkmeshFaceTypesCommand|WalkmeshStatsCommand`
  against `docs/refactoring/STATE.md` to check whether the BWM-parsing
  duplication I found had already been scoped and decided. It had: Phase 1
  candidate 16 explicitly deferred "BWM-reader dedup with WalkmeshStatsCommand"
  to Phase 2, and Phase 2's B7 executed it by creating `Core/BwmFile.cs`
  ("hoisted... 3 consumers"). I then read `Core/BwmFile.cs` in full (out of
  my batch scope — Core/ belongs to a different agent — read only as context)
  to see exactly what it publishes, and checked whether the three consumers
  in my batch actually use that surface. Two only partially do — that gap is
  A1 below; it is not a re-litigation of the settled Phase 2 item, it's what
  Phase 2 left on the table.

## Section A — general low-level cleanup

### A1 — Two of three BWM consumers bypass the Phase-2 BwmFile constants for header offsets (WalkmeshStatsCommand.cs:258-261,275-280; WalkmeshFaceTypesCommand.cs:82,84,86-87,123-128)

What's there now: `Core/BwmFile.cs` (created by the already-executed Phase 2
candidate B7, specifically to end BWM-header triplication across these three
commands) publishes named constants for every header field —
`OffType`, `OffVertexCount`, `OffVertexOffset`, `OffFaceCount`, `OffFaceOffset`,
`OffFaceTypeOffset`, plus `MinLength`, `Magic`, and a ready-made
`HasValidHeader(bytes)` check. `WalkmeshGeometryAuditCommand.cs` (the third
former consumer) fully adopted the extraction — it calls
`WalkmeshGeometryAnalysis.AnalyseRoom(path, cellSize)` and touches no raw
bytes itself.

The other two only pulled in half of `BwmFile`'s surface via
`using static Kdev.BwmFile;` — they use `ReadU32`/`ReadF32`/`WalkableTypes`,
but still hand-roll:
- the magic+length validation:
  `WalkmeshStatsCommand.cs:259-261` — `if (bytes.Length < 0x88) throw ...; if (Encoding.ASCII.GetString(bytes, 0, 8) != "BWM V1.0") throw ...;`
  `WalkmeshFaceTypesCommand.cs:82` and `:124` — same check, two more times, inline.
- every header offset as a raw hex literal instead of the named constant:
  `WalkmeshStatsCommand.cs:275-280` — `ReadU32(bytes, 0x08)`, `0x48`, `0x4C`, `0x50`, `0x54`, `0x58`.
  `WalkmeshFaceTypesCommand.cs:84,86-87` (single-file mode) and `:125,127-128` (dir mode) — the same `0x08`/`0x50`/`0x58` literals, twice more.

Why it's a problem: this is exactly the brief's Section A criterion "magic
numbers that already have a named constant elsewhere." It's also the
specific gap left by B7 — the whole point of hoisting the header knowledge
was so a header-layout question only has one place to look; right now a
reader has to know that `0x08`/`0x48`/`0x4C`/`0x50`/`0x54`/`0x58` in these two
files mean the same thing as `BwmFile.OffType`/`OffVertexCount`/etc, and any
future correction to those offsets would need to be re-applied by hand in
3 call sites instead of 1.

Proposed change: in both files, replace the raw offsets with
`BwmFile.OffType` / `OffVertexCount` / `OffVertexOffset` / `OffFaceCount` /
`OffFaceOffset` / `OffFaceTypeOffset`, and replace the two hand-rolled
magic+length checks with `BwmFile.HasValidHeader(bytes)` /
`BwmFile.MinLength` / `BwmFile.Magic`.

Risk: mechanical — the constants are literally the same numeric values
already in use; this is a compiler-checked literal-to-name swap, not a
behavior change.

Estimated line delta: roughly neutral (±0 to +2 per file; some lines get
shorter, `HasValidHeader` collapses two lines into one call in three places).

### A2 — StripDumpCommand.cs fully qualifies `System.*` where every sibling command uses bare names (StripDumpCommand.cs:32, 40-51)

What's there: this file writes `System.Console.WriteLine(...)`,
`System.Console.Error.WriteLine(...)`, and `catch (System.NotSupportedException ex)`
throughout its handler. Every other command file in the batch (all 17 of
them) uses the bare `Console`/exception-type names, relying on the project's
`ImplicitUsings` (`kdev.csproj` has `<ImplicitUsings>enable</ImplicitUsings>`,
which brings in `System` globally) the same way they do.

Why: inconsistent with the rest of the codebase for no functional reason —
there's no naming collision here (`KotorAccessibilityInstaller`, this file's
other `using`, defines no `Console` type) that would require the
qualification.

Proposed change: drop the `System.` prefixes.

Risk: mechanical.

Estimated line delta: 0 (same statements, shorter).

### A3 — optional/low-value: repeated 2-3 step extraction chain in CombatStringsExtractCommand.cs's FieldRule table

What's there: several `FieldRule`/`TrailingField` rows apply the identical
helper chain to different strrefs — e.g. `token_gesch_mod` / `token_entfernung`
/ `token_effekt` (lines ~267-272) all do
`TrimAt(StripLeading(TrimLeadingSpaces(t.Get(X)), "+ "), "<CUSTOM0>")`, and
`prefix_angriff` / `prefix_abwehr` / `prefix_schaden` (lines ~251-256) all do
`TrimAt(t.Get(X), "<CUSTOM")`.

Why flagged: the brief's Section A calls out "small repetitions inside a
file." This qualifies, narrowly.

Why I'm not recommending it: the table's value as a data-generation tool
comes from each row being self-documenting inline; adding
`PlusToken(TlkFile, int)` / `CustomPrefix(TlkFile, int)` helpers would save
very few characters per row while adding a layer of indirection a future
locale-adder has to look up. Given kdev's explicitly low ceremony bar, I'm
flagging this as visible but not actionable — leaving the call to the user.

Risk if done: mechanical. Estimated delta if done: roughly -4 lines.

## Section B — AI-pattern findings

### B1 — Redundant OS guard duplicated one frame down the call stack (LaunchCommand.cs:71-77 vs 96-98)

What's there: `Run()` already wraps both call sites in
`if (OperatingSystem.IsWindows()) { TryPin(...); ...; TryPin(...); }`
(lines 71-77). `TryPin` (line 96) then re-checks
`if (!OperatingSystem.IsWindows()) return;` as its very first statement.
Grepped `TryPin` repo-wide: it is `private`, and the only two call sites are
both already inside that `if (OperatingSystem.IsWindows())` block.

Why it's a problem: this is the belt-and-braces pattern the brief's Section B
explicitly names — "guards that duplicate a check one frame up the call
stack." It's also permanently a no-op either way: `kdev.csproj` pins
`<RuntimeIdentifier>win-x64</RuntimeIdentifier>`, so `OperatingSystem.IsWindows()`
is always true at runtime for this tool regardless of the guard.

Proposed change: delete the `if (!OperatingSystem.IsWindows()) return;` line
from `TryPin`.

Risk: mechanical (single unreachable guard, single grep-verified call site).

Estimated line delta: -2.

## Findings (possible bugs — user decides)

### F1 — WalkmeshFaceTypesCommand.cs single-file mode has no length guard before reading the BWM magic (line 81-83)

What's there: `Run(path)` (single-file mode) calls
`Encoding.ASCII.GetString(bytes, 0, 8)` directly with no prior length check.
`RunDir` in the same file (lines 122-124) checks `bytes.Length < 0x88` before
doing the equivalent read on every file it scans.

Why this is a behavior question, not a cleanup: passing a garbage or
truncated file (under 8 bytes) as the positional `path` argument in
single-file mode throws an unhandled `ArgumentOutOfRangeException` with a raw
.NET stack trace, instead of the tool's normal clean
`"not BWM v1.0"` / exit 1 message that `RunDir` would give for the same input.
Severity is low — this is a throwaway diagnostic command normally pointed at
known-good extracted `.wok` files — but it is a real behavior difference
between the file's own two code paths, so it belongs here rather than in
Section A. Not proposing a fix; if the user wants one, it falls out naturally
from adopting `BwmFile.HasValidHeader(bytes)` in the A1 fix above (that helper
already checks length before the magic compare).

## Candidate 28 — narrow-header include opportunities

Not applicable to this batch. C# has no analogue of the C++
`engine_offsets.h`-family aggregator-header problem: there's no "one big
header that pulls in everything, encouraged to be narrowed" pattern in this
codebase. Each command file already imports only the specific `Core`/`Models`
types it needs via ordinary `using` namespace directives (e.g.
`using static Kdev.BwmFile;` in the two walkmesh commands, direct
`WavAnalysis.*` / `WalkmeshGeometryAnalysis.*` static calls elsewhere) —
there is nothing to migrate off of.

## Files scanned with nothing to report

- Program.cs
- Models/FunctionTable.cs
- Commands/BuildCommand.cs (deliberately conservative given "load-bearing and
  subtle" caching logic per the task brief — read in full, no findings)
- Commands/AnalyzeDumpCommand.cs
- Commands/SigScanCommand.cs (read in full; consistent with the brief's note
  that it was recently and deliberately widened — no narrowing proposed, and
  no other findings)
- Commands/ReCommand.cs
- Commands/WalkmeshGeometryAuditCommand.cs
- Commands/SoundScoreCommand.cs
- Commands/DumpTextCommand.cs
- Commands/ApplyCommand.cs
- Commands/LogsCommand.cs
- Commands/CleanCommand.cs
- Commands/StatusCommand.cs
- Commands/DevCommand.cs
- Commands/KillCommand.cs
