# EngineAddresses.cs (191 lines)

Harvests every hardcoded engine address the patch depends on, for `kdev sigscan` to rebase. Two address shapes, both scanned: `hooks.toml`'s `[[hooks]] address = 0x...` detour sites (25 of them — fail-safe, since `KotorPatcher`'s byte-verification refuses a mismatch) and `constexpr uintptr_t kAddr...` C++ constants across `patches/Accessibility/*.cpp`/`*.h` (NOT fail-safe — a wrong address is `reinterpret_cast` and called, crashing into an unrelated function). Comment-stripping (line-based, not a full lexer) prevents prose-quoted addresses from being mistaken for real dependencies. Handles the wrapped-constant continuation-line shape too. Consumed by `SigScanCommand`.

## Declarations (in source order)

- L17 — `sealed record EngineAddress(uint Va, string Name, string Source, int Line, byte[]? OriginalBytes = null)`
- L37 — `static class EngineAddresses`
- L40 — `Regex ConstantRe` — matches `constexpr/const uintptr_t Name = 0x...;` on one line
- L47 — `Regex ContinuationRe` — matches a bare `0x...;` line following a pending declaration
- L51 — `Regex PendingDeclRe` — matches `constexpr/const uintptr_t Name =` with the value on the next line
- L60 — `List<EngineAddress> Collect(string sourceDir, string repoRoot)` — scans all `.cpp`/`.h` under `sourceDir` plus `hooks.toml`, per-line comment-stripped
- L123 — `string StripComments(string line, ref bool inBlockComment)` — blanks `//` and `/* */` text; does not track string literals (deemed safe given this codebase's statement shapes)
- L149 — `List<EngineAddress> ParseHooks(string path, string rel)` — parses `[[hooks]]` blocks for `address`, `function`, `original_bytes`
