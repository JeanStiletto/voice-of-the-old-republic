# DumpTextCommand.cs (308 lines)

`kdev dump-text` — captures the running game's decrypted image straight out of process memory (`ReadProcessMemory`), because the on-disk `swkotor.exe` is SteamStub-encrypted and its `.text` reads as ciphertext until the loader decrypts it at runtime. Writes `swkotor-image.bin` (RVA-indexed, not file-offset) + `swkotor-image.json` metadata to `build/re/imagedump/`, for `kdev sigscan` to consume when rebasing addresses onto another exe build. Exploits that the exe has no ASLR (fixed `ImageBase=0x400000`), so a dumped address equals the hardcoded address. Ends with a ciphertext-vs-decrypted sanity check via opcode-frequency heuristic. Depends on `PeInfo` and `GameProcess.ExeName`.

## Declarations (in source order)

- L45 — `static class DumpTextCommand`
- L47 — `Command Build()` — `--out`, `--pid`
- L72 — `int Run(DirectoryInfo? outDir, int? pid)` — resolve process → read PE headers → `PeInfo.Parse` → read each section at its RVA (guard/unmapped gaps mean the whole-range read must be per-section) → write bin+json → sanity-check `.text` decryption via `PrintableRunRatio`
  note: refuses to write a dump with no `.text` section; warns (exit 1) if any section read fails or if the decryption sanity check fails
- L217 — `double PrintableRunRatio(byte[] image, int start, int count)` — fraction of sampled bytes among common x86 opcodes (push/pop/mov/call/jmp/int3/ret); ciphertext scores ~2%, decrypted code scores much higher
- L236 — `Process? ResolveProcess(int? pid)` — by explicit pid, or the sole running `swkotor.exe` (errors if 0 or >1 without `--pid`)
- L270 — `sealed class ProcessHandle : IDisposable` — thin `OpenProcess`/`ReadProcessMemory`/`CloseHandle` P/Invoke wrapper
