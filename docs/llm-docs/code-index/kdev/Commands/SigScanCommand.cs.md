# SigScanCommand.cs (524 lines)

`kdev sigscan` — rebases every hardcoded engine address the patch depends on (25 hook-site detours from `hooks.toml` + `constexpr uintptr_t kAddr*` constants from the C++) onto a different `swkotor.exe` build (e.g. the Allard Russian exe). Two halves: `--emit` builds relocation-tolerant signatures from a reference image (from `kdev dump-text`, since the on-disk exe is SteamStub-encrypted) via `Signatures.Build`, cross-checking the 25 hook signatures against `hooks.toml`'s recorded `original_bytes` as a free correctness check; `--resolve` (implicit when `--target` given) searches the target exe's `.text` for each signature via `Signatures.CountMatches`, plus an "ordinal" fallback for byte-identical (linker-folded) functions matched by address rank when both binaries have the same match count. Never guesses a best candidate on ambiguity — most addresses are `reinterpret_cast` to function pointers and called, so a wrong answer crashes rather than degrading. Emits `sigscan-report.txt`/`.json`, a generated C++ rebase table (`engine_rebase_table.inc`), and a generated `target.hooks.toml`. Depends on `EngineAddresses`, `PeInfo`, `Signatures`, `KdevConfig`.

## Declarations (in source order)

- L45 — `static class SigScanCommand`
- L47 — `Command Build()` — `--reference`, `--target`, `--out`, `--emit-only`
- L75 — `int Run(FileInfo? refFile, FileInfo? targetFile, DirectoryInfo? outDir, bool emitOnly)` — loads reference image, determines memory-dump-vs-on-disk layout via size heuristic, collects addresses via `EngineAddresses.Collect`, dedupes by VA, builds signatures for in-`.text` addresses (skips data addresses), cross-checks hook bytes, optionally resolves against `--target`, writes reports
  note: warns if the reference is a SteamStub'd on-disk file (has a `.bind` section) — signatures from ciphertext are meaningless
- L222 — `bool IsIn(PeInfo pe, PeSection sec, uint va)`
- L230 — `string? CheckAgainstOriginalBytes(EngineAddress a, Signature sig)` — compares generated signature's concrete bytes against `hooks.toml`'s recorded bytes
- L243 — `int Resolve(...)` — searches target `.text` per signature; classifies `unique`/`not-found`/`ambiguous`; then an ordinal fallback pass for ambiguous/build-failed signatures matching identical hit-counts between reference and target, sorted and paired by rank
  note: ordinal matches are inference, never folded into plain `unique` — labelled `unique-ordinal` with a note
- L372 — `void WriteReports(string dest, List<SigRecord> records, string refPath, string? targetPath)` — writes `sigscan-report.txt`/`.json`
- L438 — `void EmitArtifacts(string dest, List<SigRecord> records, string targetPath)` — generates `engine_rebase_table.inc` (sorted C++ VA-pair table) and `target.hooks.toml` (per-hook block copying `original_bytes`, needs `target_versions` SHA-256 filled by hand)
- L512 — `sealed class SigRecord(EngineAddress address, Signature? signature, string status, string? note, uint? resolved, List<uint>? candidates)`
