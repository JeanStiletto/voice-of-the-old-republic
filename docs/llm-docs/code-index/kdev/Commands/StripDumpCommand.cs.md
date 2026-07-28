# StripDumpCommand.cs (58 lines)

`kdev strip-dump <in> <out>` — validation harness for `KotorAccessibilityInstaller.MinidumpStripper` (the same stripper the end-user installer runs before bundling a crash dump for the "Collect logs" feature). Strips a minidump down to just `swkotor.exe` + our DLLs' memory, all thread stacks, crash-referenced heap, contexts, and module list; drops stock system/driver module code+data. Reports before/after size and kept/dropped range counts.

## Declarations (in source order)

- L14 — `static class StripDumpCommand`
- L16 — `Command Build()` — `dump` and `out` positional args
- L26 — handler — validates input exists, calls `MinidumpStripper.StripFile(input, output)`, prints size reduction stats
  note: catches `NotSupportedException` specifically (exit 3) for dump formats the stripper can't handle, distinct from a generic failure
