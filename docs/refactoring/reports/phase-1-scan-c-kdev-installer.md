# Phase 1 structure scan — tools/kdev and installer/KotorAccessibilityInstaller

Scope: the two C# projects, `obj/`/`bin/` excluded. Method: read the
2026-07-27 code-index summaries for every file (cross-checked line counts
against the ones supplied in the task), opened source only to confirm
specific findings. Structure only — no dead-code, duplication, or behavior
findings here (those are later phases; one-line defer notes below where
duplication was noticed in passing).

## Index verification (step 1)

Every `.md` entry in `docs/llm-docs/code-index/kdev/` (including
`Commands/` and `Models/`) corresponds to a real `.cs` file, and every real
`.cs` file under `tools/kdev/` has a matching index entry. Same result for
`docs/llm-docs/code-index/installer/` (including `ModInstallers/`) against
`installer/KotorAccessibilityInstaller/`. No stale index entries, no
missing index entries. This includes the pairs called out as worth
double-checking:

- `GameVersionSelection.cs` (data record) and
  `GameVersionSelectionForm.cs` (the wizard screen) are both real, distinct
  files — not a duplicate pair.
- `ModSelection.cs` (data record) and `ModSelectionForm.cs` (the wizard
  screen) — same pattern, both real.
- `CancelConfirm.cs`, `LanguageDetector.cs`, `TslrcmDetector.cs` — all real,
  single-purpose files with matching index entries.

No index-hygiene findings to report.

## tools/kdev findings

### C1 — SoundScoreCommand.cs (1348 lines) bundles a CLI command with a
self-contained acoustic-analysis engine

The file has two clearly separable halves: the `SoundScoreCommand` static
class (CLI wiring, `Run`, report/CSV writers — L78-475, ~400 lines) and the
nested `WavAnalysis` static class (L512-1347, ~835 lines) — a complete
RIFF/WAVE reader, STFT engine, ADPCM decoder, and loop-crafting toolkit
that never references anything else in `SoundScoreCommand` except through
its own return values. `WavAnalysis` is self-contained (confirmed: no
other kdev file references `ReadMono`/`Fft`/`WavAnalysis.` — single
consumer today, but the class carries zero dependency on the command's CLI
layer).

Proposal:
- Move `WavAnalysis` (and its private helpers `HannWindow`, `Fft`,
  `SliceCentroid`, `ZcrHz`, `NextZeroCrossing`/`PrevZeroCrossing`,
  `OnsetRate`, `ReadMono`, `ReadSample`, `DecodeImaAdpcmMono` +
  `ImaIndexTable`/`ImaStepTable`) into a new top-level `WavAnalysis.cs`
  (or `tools/kdev/Audio/WavAnalysis.cs` if the folder-org proposal in K1
  below is adopted).
  - Optional finer split inside that engine: `Score`/`OnsetRate` (scoring)
    vs. `DescribeLoopability`/`MakeLoop`/`RepeatWav`/`FlattenRegion`/
    `DetectSteadyRegion`/`WriteWavMono16` (loop-crafting) are two distinct
    concerns sharing only the low-level RIFF/FFT primitives — could become
    `WavScoring.cs` + `WavLoopCrafting.cs` + `WavAnalysis.cs` (shared
    primitives), but this is optional; the acoustic engine reads fine as
    one file, it's the command-vs-engine split that pays for itself.
- `SoundScoreCommand.cs` keeps `Build`/`Run`/`WriteReport`/`WriteCsv`/
  `CopyBest`/`Tier`/`CountTiers`/`SoundScore` (the CLI+reporting surface,
  ~470 lines) and calls into `WavAnalysis`.

Risk: mechanical (move + `internal`/visibility adjustment; no logic
change). Confidence: high — the seam is exact (nested static class with no
back-references into the outer class).

### C2 — WalkmeshGeometryAuditCommand.cs (999 lines) bundles CLI/report
plumbing with a geometry-algorithm core

Declarations split cleanly: `Build`/`Run`/`PrintGlobal`/`WriteReport`/
`AppendDesignDecisions` (L50-409, the CLI+report surface, including the
fixed "Design decisions" appendix text) vs. the geometry engine — `V2`,
`Tri`, `RoomResult`, `ClusterStats`, `AreaResult`, `AnalyseRoom`,
`BumpEdge`, `AngleBin`, `TriArea`, `MinInteriorAngleDeg`, `Dist`,
`PointInTri`, `PackCell`/`UnpackCell`, `SparseRasterArea`,
`SparseRasterise`, `SparseBfs`, `ComputeSmoothness`,
`ConnectedComponents8`, `ReadU32`/`ReadF32` (L410-999, ~590 lines) — which
has no dependency on the command's CLI or reporting code.

Proposal: extract the geometry engine (BWM parsing + analysis, from
`RoomResult` through `ReadF32`) into `Commands/WalkmeshGeometryAnalysis.cs`
(or a shared location — see defer note below), leaving
`WalkmeshGeometryAuditCommand.cs` with `Build`/`Run`/`PrintGlobal`/
`WriteReport`/`AppendDesignDecisions`.

Risk: mechanical. Confidence: high, same shape of seam as C1.

Defer note (Phase 2, duplication): `WalkmeshGeometryAuditCommand.cs` and
`WalkmeshStatsCommand.cs` (503 lines) both parse BWM v1.0 directly and
both carry their own `ReadU32`/`ReadF32`, a 2D vector struct, and a
`WalkableTypes` set — the geometry-audit file's own comment already notes
this is "kept duplicated intentionally; hoist only if a 3rd consumer
appears." Worth a shared `BwmReader`/`WalkableTypes` module if the
geometry-engine extraction above happens anyway (both new engine files
would sit next to each other), but that's a Phase 2 call, not this one.

### C3 — CombatStringsExtractCommand.cs (877 lines): standalone TLK reader
embedded in a command file

`sealed class TlkFile` (L830-877, file-scope, not a member of
`CombatStringsExtractCommand`) is a minimal, generically-useful TLK
reader (12-byte header, 40-byte string-data table, `Get(strref)`) with no
dependency on the rest of the file. Confirmed via grep: no other kdev
command currently reads a TLK file, so this is not yet a
"shared-but-hidden" duplication problem — but it is a self-contained
low-level format reader sitting inside a command file, the same shape as
`PeInfo.cs` (shared PE reader) and `Signatures.cs` (shared signature
engine) already living at top level.

Proposal: move `TlkFile` to its own top-level `TlkFile.cs`, matching the
`PeInfo.cs`/`Signatures.cs` convention for "engine-format reader used by
exactly one or two commands but conceptually reusable." Low urgency since
there's only one consumer today; worth doing at the same time as C1/C2
since it's the same kind of seam and a future combat/dialog-string command
would otherwise be tempted to hand-roll a second TLK reader.

Risk: mechanical. Confidence: medium (the split is clean; the value is
speculative since there's no second consumer yet).

### C4 — BuildCommand.cs (627 lines): two build strategies + toolchain
discovery in one file

`RunIncremental` (the default per-TU cached build) and `RunViaBat` (legacy
full-rebuild via `create-patch.bat`) are two independent code paths for
the same command, both calling into shared MSVC-toolchain-discovery
helpers (`FindVcvars32`, `CaptureEnv`, `FindOnPath`, `RunCl`) and
`BuildLoader`/`CopyDirectory`. Confirmed via grep: `vcvars32`/toolchain
helpers are only used inside this file today.

Proposal (optional, lower priority than C1-C3): extract the MSVC toolchain
discovery (`FindVcvars32`, `CaptureEnv`, `FindOnPath`, `RunCl`,
`CompileFlags`/`LinkFlags` constants) into a `MsvcToolchain.cs`, leaving
`BuildCommand.cs` with `Build`/`Run`/`RunIncremental`/`NeedsRecompile`/
`CompileChanged`/`WriteDeps`/`FilterNotes` (incremental path),
`RunViaBat`/`CopyDirectory` (legacy path), and `BuildLoader` (shared by
both). This is a smaller win than C1/C2 since 627 lines isn't badly over
threshold and the two paths are genuinely two branches of the same command
rather than a mixed responsibility — flagging for completeness, not
urging it.

Risk: mechanical. Confidence: medium — the split is clean but the payoff
is marginal at this size.

### C5 — AnalyzeDumpCommand.cs (677 lines): hand-rolled minidump-format
reader embedded in the command

`ExceptionRecord`, `ReadExceptionStream` (hand-parses
`MINIDUMP_EXCEPTION_STREAM` because ClrMD doesn't expose it), and `Ctx32`
(x86 `CONTEXT` struct, fixed-offset extraction) — L536-716, ~180 lines —
form a self-contained low-level minidump reader, the same shape as C3's
`TlkFile`. Single consumer today.

Proposal (optional, lowest priority of the five): extract to
`MinidumpException.cs` or similar. Not urging this one — the command is
already cohesive around "minidump investigation," and unlike C1-C3 the
reader is tightly bound to this command's specific use case (fault-time
register recovery), so the reuse argument is weaker.

Risk: mechanical. Confidence: low-medium.

### K1 — kdev top-level files are ungrouped while `Commands/`/`Models/`
already establish a folder convention

`Config.cs`, `EngineAddresses.cs`, `GameProcess.cs`, `PeInfo.cs`,
`Signatures.cs` are shared low-level infrastructure consumed across
multiple commands (`Config` by nearly everything; `PeInfo`/`Signatures` by
`SigScanCommand` and `DumpTextCommand`; `EngineAddresses` by
`SigScanCommand`; `GameProcess` by `ApplyCommand`/`CleanCommand`/
`KillCommand`/`DumpTextCommand`). `Models/` currently holds exactly one
file (`FunctionTable.cs`, consumed by `AnalyzeDumpCommand`). Namespaces are
already `Kdev` (top-level) vs `Kdev.Models` vs `Kdev.Commands`, so a folder
reorg here is a real namespace change, not just a file move.

Proposal (optional): a `Core/` folder (`Kdev.Core` namespace) holding
`Config.cs`, `EngineAddresses.cs`, `GameProcess.cs`, `PeInfo.cs`,
`Signatures.cs`, and the C1/C2/C3 extractions above (`WavAnalysis.cs`,
`WalkmeshGeometryAnalysis.cs`, `TlkFile.cs`) would make the project layout
read as "Program.cs (entry) + Core/ (shared infra) + Commands/ (CLI
surface) + Models/ (Ghidra-derived data)." `Program.cs` stays at the root
per convention (entry point). This is purely organizational — 6 files at
~150 lines average isn't causing real pain today, so treat as optional
polish, not a blocker.

Risk: needs build verification (namespace changes ripple into every
`using Kdev;` — mechanical but not zero-risk; a full `dotnet build` after
the move is the correct check, no behavior change expected).
Confidence: medium — reasonable convention, but genuinely optional given
the current file count.

No other kdev files (`ReCommand.cs`, `SigScanCommand.cs`,
`WalkmeshStatsCommand.cs`, `DumpTextCommand.cs`, and everything else under
100-250 lines) show a split-worthy seam or a misplaced responsibility.
Naming is consistent: all 18 subcommands end in `Command`, and top-level
infra files carry no forced suffix (appropriate — they're not commands).

## installer/KotorAccessibilityInstaller findings

### I1 — Program.cs (1037 lines) mixes five distinct responsibilities

`Program.cs` is the single largest file in either project and covers:
1. CLI entry point (`Main`, arg parsing, admin/running-game gates, mode
   dispatch) — L13-262.
2. Install-flow orchestration (`RunFullInstallFlow`,
   `RunKotor2PreparationFlow`, `RunTslrcmInstall`,
   `ApplyKotor2EnginePatches`, `RunTslrcmWizardInteractive`,
   `TryDeleteTempFile`, `WarnIfRussianTranslationMissing`,
   `CollectLogsAndReport`, `ToggleSpatialAudioAndReport`) — L262-745, ~480
   lines, the largest chunk.
3. Uninstall (`PerformUninstall`, `ScheduleUninstallerSelfDelete`) —
   L746-852.
4. Game-path detection and validation (`IsRunningAsAdmin`,
   `IsGameRunning`, `DetectGamePath`, `TryReadSteamAppInstallPath`,
   `DetectKotor2GamePath`, `TryReadSteamKotor2InstallPath`,
   `IsValidKotor2GamePath`, `IsValidGamePath`, `IsSteamPath`) — L852-1001.
5. Version-string comparison (`IsNewerVersion`, `NormalizeVersion`) —
   L1001-1037.

This is the installer's equivalent of C1/C2 in kdev — a file that grew by
accretion around the entry point rather than by design. Every one of these
five groups is already named like it expects to live in its own class
(the method names read like a class's public surface).

Proposal, matching the existing `*Manager`/`*Detector` naming convention
already used elsewhere in this project (`InstallationManager`,
`RegistryManager`, `SpatialAudioManager`, `LanguageDetector`,
`TslrcmDetector`, `GameLocaleDetector`):
- `GamePathDetector.cs` — `DetectGamePath`, `TryReadSteamAppInstallPath`,
  `DetectKotor2GamePath`, `TryReadSteamKotor2InstallPath`,
  `IsValidGamePath`, `IsValidKotor2GamePath`, `IsSteamPath`,
  `IsGameRunning`, `IsRunningAsAdmin`, and `GameExeName`/`DefaultGamePath`
  (the constants these methods close over).
- `InstallFlow.cs` (or `InstallOrchestrator.cs`) —
  `RunFullInstallFlow`, `RunKotor2PreparationFlow`, `RunTslrcmInstall`,
  `ApplyKotor2EnginePatches`, `RunTslrcmWizardInteractive`,
  `TryDeleteTempFile`, `WarnIfRussianTranslationMissing`,
  `CollectLogsAndReport`, `ToggleSpatialAudioAndReport`.
- `UninstallFlow.cs` — `PerformUninstall` (must stay `public`, called from
  `UninstallForm`) and `ScheduleUninstallerSelfDelete`. Small (2 methods,
  ~110 lines) but conceptually distinct from install-flow and worth
  separating since `UninstallForm` only needs this half.
- `VersionComparer.cs` — `IsNewerVersion`, `NormalizeVersion`. Small (2
  methods, ~35 lines); could also just stay in `Program.cs` if a
  dedicated file feels like overkill for 35 lines — flagging as optional
  within this optional split.
- `Program.cs` keeps `Main`, `UpdateChoice` enum, and becomes purely the
  entry point + top-level mode dispatch (~260 lines).

Risk: needs build verification — these are `private static` methods
today; moving them to new classes requires either making them `internal
static` on the new class or keeping them as extension-style static
methods, and every call site inside the old `Program.cs` plus the handful
of external callers (`UninstallForm` calls `Program.PerformUninstall`,
other forms may reference `Program.IsSteamPath`/`Program.DetectGamePath`
etc.) needs updating. Purely mechanical (no logic change) but touches
many call sites — a full build + the existing manual install/uninstall
test pass is the right verification, not just a compile check.
Confidence: high that the split seams are correct (grouped by the method
names' own subject matter); medium on exact file boundaries (the
install-flow group is large enough it could be split further, e.g.
separating the KOTOR 2 preparation/TSLRCM sub-flow from the KOTOR 1 flow,
but that's a judgment call for whoever implements it).

### I2 — Forms folder: noted, not recommended

The task asks whether the installer's 12 `*Form.cs` files (`MainForm`,
`GameVersionSelectionForm`, `Kotor2ModSelectionForm`,
`InstalledOptionsForm`, `Kotor2ModsInstallForm`, `ModSelectionForm`,
`TslrcmInstallForm`, `UninstallForm`, `UpdateAvailableForm`,
`WelcomeForm`, `WorkshopTlkHarvestForm`, `ModdingInfoForm`) should move
into a `Forms/` folder, mirroring `ModInstallers/`.

Checked against the sibling arena installer
(`C:\Users\fabia\Dev\arena\installer\AccessibleArenaInstaller`), which is
the explicitly-intentional model for this project: its 13 top-level
`.cs` files (including `MainForm.cs`, `WelcomeForm.cs`, `UninstallForm.cs`,
`UpdateAvailableForm.cs`) are **fully flat** — no `Forms/` folder, no
`Detectors/`/`Clients/` folders, only non-code `Locales/`/`Resources/`
folders. `ModInstallers/` is itself a deviation from that flat model,
presumably justified by the `IModInstaller` plugin-interface pattern
(4 implementations + coordinator + shared helpers — a genuinely distinct
subsystem, not just "many files of one kind").

Given the explicit instruction that matching the arena model is
intentional, not a smell: I'm not recommending a `Forms/` (or
`Detectors/`/`Clients/`) folder. Noting it as considered-and-declined
rather than silently skipping the question, since the task asked directly.
If the team wants to diverge from the arena model going forward, this
would be the mechanical, low-risk way to do it (namespace can stay flat
`KotorAccessibilityInstaller` — folders don't need to imply
sub-namespaces, `ModInstallers/` does use `KotorAccessibilityInstaller.
ModInstallers` but that's not forced).

Confidence: high that flat is the intentional, established convention;
this is a "don't do it" finding, included for completeness.

### I3 — MainForm.InstallButton_Click is a ~330-line method (L218-546)

Noted for completeness, not filed as a file-structure finding: this one
method covers the ~30-numbered-step install pipeline. It's inside a
650-line file that's otherwise cohesive (one form, one job — drive the
install). Splitting a single large method into several private methods on
the same class is a method-level (not file-level) change and arguably
belongs to a later phase alongside other in-file readability work, not
Phase 1's file/folder structure pass. Flagging so it isn't lost, not
proposing a file split here — `MainForm.cs` at 650 lines with one
job is not mis-structured the way `Program.cs` is.

Risk: n/a (not proposing an action this phase). Confidence: n/a.

### I4 — GameLocale.cs holds both the `GameLocale` enum and the
`GameLocaleDetector` static class

Minor naming note: the file is named after the data type (`GameLocale`),
but also contains `GameLocaleDetector` (the detection logic), which by
the project's own `LanguageDetector.cs`/`TslrcmDetector.cs` convention
might expect its own file. However, `GameLocale.cs` is small (124 lines)
and the enum + its one detector are tightly coupled (the file's own
docstring stresses the threshold constants must stay in lockstep with
`TlkLooksCyrillic` in the C++ side) — splitting would separate two things
that change together for the same reason. Comparable to `ModSelection.cs`
pairing a data record with logic used by its forms. Not recommending a
split; noting only because the task asked about naming-consistency in
this area.

Risk: n/a. Confidence: low (this is closer to "no action" than a real
finding).

No other installer files show a split-worthy seam. `InstallationManager.cs`
(599), `GitHubClient.cs` (432), `LogCollector.cs`/`MinidumpStripper.cs`
(417/284, cleanly paired producer/shrinker), `TslrcmInstallForm.cs` (376),
`K1cpInstaller.cs` (293), `WorkshopTlkHarvestForm.cs` (290),
`PriorityGroup2da.cs` (266), `SwkotorIniTweaker.cs` (259),
`SpatialAudioManager.cs` (249), `ModSelectionForm.cs` (244),
`IntroMovieDisabler.cs` (242), `WelcomeForm.cs` (226),
`Kotor2ModSelectionForm.cs` (217), `Config.cs` (214), and everything under
200 lines each cover one job cleanly (confirmed against their code-index
summaries). The `ModInstallers/` subsystem (`IModInstaller`,
`ModInstallerCoordinator`, `ModInstallContext`, `ModInstallResult`,
`K1cpInstaller`, `K2cpInstaller`, `TweakPackInstaller`,
`HoloPatcherProvider`, `HoloPatcherRunner`,
`GitHubTslpatchdataFetcher`) is well-factored: one interface, one
coordinator, three installers each doing exactly one mod, two genuinely
shared helpers (`HoloPatcherRunner`, `GitHubTslpatchdataFetcher`)
correctly pulled out because they *are* used by multiple installers
(confirmed in the index: K1cp/K2cp/TweakPack all reference
`HoloPatcherRunner`; K1cp/K2cp reference `GitHubTslpatchdataFetcher`) —
this is exactly the "helper needed by other commands" pattern done
right, contrasted with kdev's C1/C2/C3 above where the equivalent
helpers are still single-consumer and embedded.

Naming is consistent: 12/12 Form classes end in `Form`; `ModInstallers/`
uses `Installer`/`Provider`/`Runner`/`Fetcher`/`Coordinator`/`Context`/
`Result` suffixes matching each class's actual role; `*Manager`
(`InstallationManager`, `RegistryManager`, `SpatialAudioManager`) is used
for classes that own a persistent piece of installed state, while
`*Tweaker`/`*Disabler`/`*Stripper`/`*Detector`/`*Client` are used for
narrower single-purpose static helpers — this reads as intentional
differentiation (the suffix communicates what the class does), not
inconsistency, so not flagging it as one.

## Summary of proposals by risk/priority

Recommended (clear win, mechanical, high confidence):
- C1 — split `SoundScoreCommand.cs`'s `WavAnalysis` engine out
- C2 — split `WalkmeshGeometryAuditCommand.cs`'s geometry engine out
- I1 — split `Program.cs` into path-detection / install-flow /
  uninstall-flow (+ optionally version-compare)

Optional / lower priority (real but smaller payoff):
- C3 — extract `TlkFile` from `CombatStringsExtractCommand.cs`
- C4 — extract MSVC toolchain discovery from `BuildCommand.cs`
- C5 — extract the minidump-exception reader from `AnalyzeDumpCommand.cs`
- K1 — group kdev's top-level infra files into `Core/`

Considered and not recommended:
- I2 — a `Forms/` folder for the installer (breaks from the intentional
  flat arena-installer convention)

Noted, no action proposed this phase:
- I3 — `MainForm.InstallButton_Click`'s method length (method-level, not
  file-level)
- I4 — `GameLocale.cs` housing both the enum and its detector (tight,
  intentional coupling)
