# Phase 3 scan — kdev shared core (tools/kdev/Core/)

Scope: WavAnalysis.cs (853), WalkmeshGeometryAnalysis.cs (608),
EngineAddresses.cs (319), Signatures.cs (262), Config.cs (241), PeInfo.cs
(135), GameProcess.cs (71), TlkFile.cs (63), BwmFile.cs (62), SoundScore.cs
(35). All ten read in full. `bin/`, `obj/` not touched.

Method: full read of every file in the batch, then targeted greps against
the whole `tools/kdev` tree (not just this batch) to verify call sites
before calling anything dead or duplicated — this batch is almost entirely
consumed by files in `Commands/`, so a batch-only grep would have produced
false positives. Greps run (each against `C:\Users\fabia\Dev\kotor\tools\kdev`
unless noted):
- `BwmFile\.|ReadU32|ReadF32|WalkableTypes|HasValidHeader` — find every BWM consumer.
- `OffType|OffVertexCount|OffVertexOffset|OffFaceCount|OffFaceOffset|OffFaceTypeOffset|HasValidHeader|MinLength|BwmFile\.Magic|\bMagic\b` — check whether BwmFile's named constants/validator are actually called anywhere.
- `WavAnalysis\.`, `WalkmeshGeometryAnalysis\.|AnalyseRoom`, `EngineAddresses\.|Signatures\.`, `TlkFile\.|new TlkFile`, `GameProcess\.|SoundScore\b`, `PeInfo\.|KdevConfig\.|Config\.Load` — confirm each Core type's caller set.
- `IsRunning` (bare, whole tree) and `GameProcess\.ExeName|GameProcess\.ProcessName` — check every member of GameProcess for real callers.
- `NothingToKill|AllKilled|KillSummary`, `ConcreteBytes`, `CollectHandResolved|EngineAddress\(|\.OriginalBytes`, `\.ReSarifPath|\.ReJqPath|...` (all KdevConfig record fields), `\.SizeOfImage|\.Timestamp\b|TimestampUtc|SectionNamed` — dead-public-surface sweep on the remaining files.
- `CultureInfo|NumberStyles|Globalization` scoped to WalkmeshGeometryAnalysis.cs, and `Iced\.Intel|using Iced` scoped to Signatures.cs — unused-`using` check.
- `ComputeSmoothness` (whole tree) — confirm single call site before flagging a redundant guard.
- `DiagLockedClusters|ClusterStats|RectilinearFractionLocal\b|LocalBestRotationDeg` scoped to WalkmeshGeometryAuditCommand.cs — confirm the RoomResult/AreaResult fields this batch produces are actually read downstream.

Read in full (outside the batch, to verify consumer claims):
`Commands/WalkmeshStatsCommand.cs`, `Commands/WalkmeshFaceTypesCommand.cs`,
relevant excerpts of `Commands/DumpTextCommand.cs`.

## Section A — general low-level cleanup

### A1 — Unused `using System.Globalization;` (WalkmeshGeometryAnalysis.cs:2)

What's there: `using System.Globalization;` at the top of the file.
Nothing in the file references `CultureInfo`, `NumberStyles`, or any other
Globalization type — grep for all three inside the file returns only the
`using` line itself.

Why it's a problem: extraction residue. The file was split out of
`WalkmeshGeometryAuditCommand.cs` (which does use `CultureInfo` for
`ToString("F0", CultureInfo.InvariantCulture)` in its report formatting);
the `using` came along but the code that needed it stayed in the command
file.

Proposed change: delete line 2.
Risk: mechanical. Line delta: -1.

### A2 — Stale "kept local" comment, now contradicted by the code right below it (WalkmeshGeometryAnalysis.cs:113-115)

What's there:
```
internal static class WalkmeshGeometryAnalysis
{

    // Same set as WalkmeshStatsCommand. Kept local to keep this command
    // self-contained; if a third walkmesh consumer appears, hoist into a
    // shared helper at that point.

    internal static RoomResult? AnalyseRoom(string path, float cellSize)
```

Why it's a problem: this is the exact comment `Core/BwmFile.cs`'s own
header narrates as history — "The geometry file even carried a comment
saying 'kept duplicated intentionally; hoist only if a 3rd consumer
appears' — by the time the Phase-2 duplication scan looked, the third
consumer already existed." B7 executed that hoist (`WalkableTypes` now
lives in `BwmFile.cs` and reaches this file via
`using static Kdev.BwmFile;` at line 4). The comment was never deleted, so
it now sits directly above code that contradicts it: `WalkableTypes` is no
longer local, and a third consumer (`WalkmeshFaceTypesCommand.cs`) already
exists on top of the original two.

Proposed change: delete the three-line comment block (lines 113-115) and
the blank line above it if desired; nothing needs to replace it since the
class-level doc comment (lines 8-19) already explains the BwmFile split.
Risk: mechanical (comment-only). Line delta: -3 to -4.

### A3 — BwmFile's own header-validity helpers are dead; all three consumers duplicate the check by hand instead (BwmFile.cs:59-61, cross-referenced against WalkmeshGeometryAnalysis.cs:120-121, WalkmeshStatsCommand.cs:259-261, WalkmeshFaceTypesCommand.cs:82-83/124)

What's there: `BwmFile.cs` publishes `HasValidHeader(byte[])`, `Magic`,
`MinLength`, and six named offset constants (`OffType`,
`OffVertexCount`, `OffVertexOffset`, `OffFaceCount`, `OffFaceOffset`,
`OffFaceTypeOffset`). A repo-wide grep for every one of those names finds
only their own declarations in BwmFile.cs — zero callers anywhere in
`tools/kdev`.

Instead, every one of the three consumers hand-rolls the same
length-and-magic check and hardcodes the same six hex offsets that BwmFile
already named:
- `WalkmeshGeometryAnalysis.cs:120-128` —
  `if (bytes.Length < 0x88) return null; if (Encoding.ASCII.GetString(bytes, 0, 8) != "BWM V1.0") return null;`
  then `ReadU32(bytes, 0x08)` / `0x48` / `0x4C` / `0x50` / `0x54` / `0x58`.
- `WalkmeshStatsCommand.cs:259-280` — the identical two-line check, the
  identical six offsets, plus a 12-line header-layout doc comment
  (lines 263-274) that duplicates BwmFile.cs's own header comment
  (lines 4-22) almost line for line.
- `WalkmeshFaceTypesCommand.cs:82-87` (single-file `Run`) and `:124-128`
  (`RunDir`) — same magic-string literal, same `0x08`/`0x50`/`0x58`.

Why it's a problem: this is exactly the residue the brief's B7 background
flags as a high-value thing to check for — B7 hoisted the byte-reading
primitives (`ReadU32`/`ReadF32`/`WalkableTypes`) so the three consumers
genuinely share those now, which is real progress. But the validity check
and the offset table — the two things `HasValidHeader` and the `Off*`
constants exist specifically to centralise — never got adopted. Today, if
the BWM header layout ever needed a comment correction or the magic
string ever needed adjusting, it would need editing in four places
(BwmFile.cs plus all three consumers) instead of one, and the "kept
local" duplication problem B7 was created to solve is still fully present
for these two pieces, just invisible because it doesn't show up as
duplicate *primitive* implementations.

Proposed change (three small, independent edits):
- `WalkmeshGeometryAnalysis.cs:120-121` → `if (!HasValidHeader(bytes)) return null;`
- `WalkmeshStatsCommand.cs:259-261` → `if (!HasValidHeader(bytes)) throw new InvalidDataException("not a BWM v1.0 file");`, and swap the six literal offsets at :275-280 for `OffType`/`OffVertexCount`/etc.
- `WalkmeshFaceTypesCommand.cs:82-83` and `:124` → `if (!HasValidHeader(bytes)) { ... }`, and swap the offset literals for the named constants throughout.
- Also swap the six offset literals in `WalkmeshGeometryAnalysis.cs:123-128` for the named constants (currently uses raw hex even though it already imports BwmFile statically).

Risk: low — mechanical value substitution, no logic change, each edit
independently `dotnet build`-checkable. Estimated line delta: roughly -10
to -15 net across the three consumer files once the duplicated header
comment in WalkmeshStatsCommand.cs is trimmed to a one-line pointer at
BwmFile.cs.

### A4 — GameProcess.IsRunning() has no callers (GameProcess.cs:23)

What's there: `public static bool IsRunning() => Process.GetProcessesByName(ProcessName).Length > 0;`

Why it's a problem: a bare-name grep for `IsRunning` across all of
`tools/kdev` returns only this declaration. `GameProcess`'s other public
members (`ExeName`, `KillAll`, `KillSummary` and its two properties) all
have real callers in `Commands/DumpTextCommand.cs`, `KillCommand.cs`,
`ApplyCommand.cs`, `CleanCommand.cs`. This one method looks like it was
added as a natural companion to `KillAll()` but nothing ever needed it —
`DumpTextCommand.cs`'s own "is the game running" check (see A5) uses
`Process.GetProcessesByName` directly rather than this helper.

Proposed change: delete the method (and `using System.Diagnostics;` stays
— `Process` is still used by `KillAll`).
Risk: mechanical (unused public method, zero callers confirmed
repo-wide). Line delta: -1.

### A5 (cross-batch observation, not a numbered candidate for this batch) — DumpTextCommand.cs recomputes GameProcess.ProcessName instead of using the constant

What's there, in `Commands/DumpTextCommand.cs:248-249` (outside this
batch — flagging because it's directly about how a Core/ constant from
this batch is or isn't used):
```
var procs = Process.GetProcessesByName(
    Path.GetFileNameWithoutExtension(GameProcess.ExeName));
```
`GameProcess.cs` already defines `public const string ProcessName = "swkotor";`
one line above `ExeName = "swkotor.exe"`, specifically so callers don't
have to strip the extension themselves.

Why it's a problem: same value, computed the roundabout way, right next
to a same-file, same-purpose constant that would do it directly.

Proposed change (for whoever's batch owns DumpTextCommand.cs): replace
with `Process.GetProcessesByName(GameProcess.ProcessName)`.
Risk: mechanical. Line delta: -1 (collapses a two-line call to one).

## Section B — AI-pattern findings

### B1 — Orphaned duplicate `<summary>` block; the method it belongs to has no doc comment (Signatures.cs:68-77)

What's there:
```
    /// <summary>
    /// Grow a signature from <paramref name="va"/> until it is unique within
    /// the reference .text, wildcarding relocation-sensitive operands.
    /// </summary>
    /// <summary>
    /// True when an absolute address baked into an instruction can legitimately
    /// differ between two builds, and must therefore be wildcarded.
    /// .text (code moved) and .rdata (vtables/literals, section size differs)
    /// qualify; .data does not.
    /// </summary>
    private static bool IsRelocationSensitive(PeInfo pe, uint addr)
```
Two consecutive `<summary>` blocks sit on top of one method. The first
one ("Grow a signature from va...") describes `Build(...)`, which is the
public method 8 lines further down (line 85) — and `Build` currently has
no doc comment of its own. The second `<summary>` correctly describes
`IsRelocationSensitive`.

Why it's a problem: this reads as documentation that got separated from
its method during an edit (likely when `IsRelocationSensitive` was
inserted between the comment and `Build`) and never reunited. As it
stands it's actively misleading — a reader hovering `IsRelocationSensitive`
in an IDE sees a doc-comment describing something else entirely.

Proposed change: move the first `<summary>` block (lines 68-71) down to
sit directly above `public static BuildResult Build(...)` (line 85);
leave the second block where it is, on `IsRelocationSensitive`.
Risk: mechanical (comment-only, no code change). Line delta: 0 (moved,
not added/removed).

### B2 — Redundant guard duplicating the caller's check one frame up (WalkmeshGeometryAnalysis.cs:441, called from :221/:252)

What's there. The only call site:
```
221    var cells = SparseRasterise(result.Triangles, cellSize);
222    if (cells.Count > 0)
223    {
             ...
252        ComputeSmoothness(cells, cellSize, result);
             ...
277    }
```
And the callee itself:
```
439    private static void ComputeSmoothness(HashSet<long> cells, float cellSize, RoomResult result)
440    {
441        if (cells.Count == 0) return;
```
Grep confirms `ComputeSmoothness` has exactly one call site in the whole
tree, and it's already inside `if (cells.Count > 0)`.

Why it's a problem: this is the brief's Section B pattern by name —
"belt-and-braces guards that duplicate a check one frame up the call
stack." The internal guard is unreachable as written; it adds a branch a
reader has to reconcile against the caller for no behavioural benefit.

Proposed change: delete line 441. If `ComputeSmoothness` is ever meant to
become independently callable (safe to call with an empty set), that's a
design choice to make explicitly then, not to leave as silent dead code
now.
Risk: mechanical (guard is provably unreachable given the single call
site; `dotnet build` plus the existing walkmesh-audit output on a known
area would confirm no output change if a spot-check is wanted). Line
delta: -1.

## Findings (possible bugs — user decides)

### F1 — WalkmeshFaceTypesCommand.cs single-file mode can crash on a short/non-BWM file (outside this batch, but the root cause is BwmFile.cs's validator not being used — see A3)

What's there, `Commands/WalkmeshFaceTypesCommand.cs:74-83`:
```
private static int Run(string path)
{
    if (string.IsNullOrEmpty(path)) { ...; return 1; }
    var bytes = File.ReadAllBytes(path);
    if (Encoding.ASCII.GetString(bytes, 0, 8) != "BWM V1.0")
    { Console.Error.WriteLine("not BWM v1.0"); return 1; }
    uint type = ReadU32(bytes, 0x08);
```
There is no length check before `Encoding.ASCII.GetString(bytes, 0, 8)`.
The sibling `RunDir` path in the very same file (line 123) does guard
with `if (bytes.Length < 0x88) continue;` before touching the bytes — so
the single-file path is inconsistent with the directory path in the same
file, and is the one a user is more likely to hit interactively.

Why it's a problem: `Encoding.ASCII.GetString(bytes, 0, 8)` throws
`ArgumentOutOfRangeException` when `bytes.Length < 8`. Passing a short,
truncated, or wrong-extension file to `kdev walkmesh-facetypes <path>`
(single-file mode, no `--dir`) crashes with a raw .NET stack trace instead
of the intended "not BWM v1.0" message. This is exactly the failure mode
`BwmFile.HasValidHeader` was written to prevent (it checks
`bytes.Length >= MinLength` before the magic-string check).

Risk: low, CLI-only diagnostic tool, not in the play-facing patch. Verify
with a deterministic CLI repro (no game needed): create an empty or
&lt;8-byte file and run `kdev walkmesh-facetypes <that file>` — today it
should throw; after adopting `HasValidHeader` (A3) it should print
"not BWM v1.0" and exit 1.

## Candidate 28 — narrow-header include opportunities

Not applicable. Candidate 28 (migrating C++ includers off the
`engine_offsets.h`-family aggregators onto narrower headers) is a
C++-specific concern; this batch is entirely C# and has no analogous
aggregator-header pattern to report against.

## Files scanned with nothing to report

- EngineAddresses.cs — the extensive header prose is deliberate and
  documents a real prior incident (the 12-lost-addresses bug fixed in
  Phase 2); the harvester was intentionally widened and must not be
  narrowed per this batch's brief. No dead code, no unused usings, no
  duplication found.
- Config.cs — all `KdevConfig` record fields have live callers (verified
  by grepping every field name across `tools/kdev`); `Load()` vs
  `Validate()` is a deliberate, documented split, not redundant checking.
- PeInfo.cs — every public member (`SizeOfImage`, `Timestamp`,
  `TimestampUtc`, `SectionNamed`, `SectionForRva`, `VaToOffset`,
  `OffsetToVa`) has confirmed callers in `Commands/`.
- TlkFile.cs — small, single consumer (`CombatStringsExtractCommand.cs`),
  no residue.
- SoundScore.cs — small, single consumer chain through `WavAnalysis.cs`
  and `SoundScoreCommand.cs`, no residue.
- WavAnalysis.cs — large (853 lines) but the size question was already
  settled in Phase 1 (candidate 15: "no finer engine split"); no dead
  code, no unused usings, no redundant guards found on this pass.
