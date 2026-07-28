# Program.cs (49 lines)

Entry point. Forces invariant culture (so `--cell 0.5`-style numeric args don't misparse under e.g. German locale's comma decimal separator), registers the CP-1252 encoding provider (needed by `CombatStringsExtractCommand` for TLK byte decoding), builds the `System.CommandLine` `RootCommand` wiring every subcommand's `Build()`, and invokes it.

## Declarations (in source order)

- L8 — `static class Program`
- L10 — `Task<int> Main(string[] args)` — culture setup, CP-1252 registration, `RootCommand` assembly (18 subcommands: status, build, clean, apply, launch, dev, kill, logs, analyze-dump, strip-dump, walkmesh-stats, walkmesh-facetypes, walkmesh-geometry-audit, combat-strings-extract, sound-score, re, dump-text, sigscan), `root.InvokeAsync(args)`
