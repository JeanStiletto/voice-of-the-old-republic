# ReCommand.cs (520 lines)

`kdev re <pattern>` — the "reconstruct-a-subsystem" command, turning the decompile-first RE playbook into one invocation. Given a regex matched against class/namespace names, it queries Lane's SARIF export via `jq` (one pass emitting NDJSON of `FUNCTIONS` and `DATATYPE` records), classifies functions as trivial (Set*/Get*/dtor/ctor) vs logic-bearing, optionally batch-decompiles the logic-bearing set via Ghidra headless (`tools/ghidra-scripts/Decompile.java`, capped by `--max-funcs`), and emits a skeleton `<slug>-model.md` (known/suspected/open format) plus the raw decompile text — ready to read and curate into `docs/llm-docs/`. Depends on `KdevConfig`'s `[re]` section (`ReSarifPath`, `ReJqPath`, `ReGhidraHeadless`, etc.).

## Declarations (in source order)

- L48 — `static class ReCommand`
- L50 — `const string DecompMarker = "Decompile.java> "`
- L51 — `const string GhidraSuffix = "(GhidraScript)"`
- L53 — `Command Build()` — `pattern` arg, `--decompile`/`-d`, `--all`, `--max-funcs` (default 40), `--out`, `--jq`
- L103 — `sealed record FnRec(string Loc, string Ns, string Name, string Sig)`
- L104 — `sealed record FieldRec(int Offset, string Name, string Type)`
- L105 — `sealed record DtRec(string Name, List<FieldRec> Fields)`
- L107 — `int Run(...)` — query SARIF → dedupe/sort → pick decompile set → `RunGhidraDecompile` → `EmitDoc` → summary printout
- L213 — `(List<FnRec>, List<DtRec>) QuerySarif(string jq, string sarif, string pattern)` — single jq pass over the SARIF JSON, matches `FUNCTIONS` by namespace and `DATATYPE` by name against the regex
- L301 — `string RunGhidraDecompile(KdevConfig cfg, List<string> addrs)` — shells `analyzeHeadless.bat ... -postScript Decompile.java <addrs...>` via cmd.exe
- L356 — `string CleanGhidraOutput(string raw)` — strips Ghidra's headless log framing (only the first line of each multi-line println is framed) down to just the decompile body
- L403 — `bool IsTrivial(FnRec f)` — Set*/Get*/`~`dtor/ctor-name-matches-class
- L417 — `string Slug(string pattern)` — filesystem-safe slug, capped 48 chars
- L434 — `string EmitDoc(...)` — writes the model-skeleton markdown: Coverage, MODEL (fill-in), Function inventory (per class, `*` = decompiled), Struct layouts, Raw decompile pointer
