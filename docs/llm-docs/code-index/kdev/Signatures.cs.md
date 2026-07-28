# Signatures.cs (263 lines)

Builds and resolves relocation-tolerant x86 code signatures for `kdev sigscan`, using `Iced.Intel` for decoding. Two builds of the same engine share identical codegen but the linker places functions at different (non-monotonic, +96..+512 byte observed) offsets, so a signature must wildcard exactly the bytes that legitimately shift: `call`/`jmp`/`jcc` relative displacements (nearly every function has one), and absolute pointers/displacements into `.text` or `.rdata` (code moved; `.rdata`'s section size differs between builds so vtable pointers and string-literal references shift too — this was the fix for 4 constructors/destructors that failed the first pass). Displacements into `.data` deliberately stay concrete — that section is byte-stable between builds and is the strongest available discriminator. Grows a signature instruction-by-instruction (minimum 4 instructions, max 256 bytes) until `CountMatches` against the reference `.text` returns exactly 1.

## Declarations (in source order)

- L10 — `sealed record Signature(byte[] Pattern, bool[] Mask, int InstructionCount)` — `Length`, `ConcreteBytes`, IDA-style `ToString()` (`"48 8B ?? ?? E8"`)
- L53 — `static class Signatures`
- L60 — `const int MaxBytes = 256`
- L64 — `const int MinInstructions = 4` — chosen because a 5-byte pattern was observed matching 2,517 places in `.text`
- L66 — `sealed record BuildResult(Signature? Signature, string? Failure, int MatchesAtFinalLength)`
- L78 — `bool IsRelocationSensitive(PeInfo pe, uint addr)` — true for `.text`/`.rdata` addresses, false for `.data`
- L85 — `BuildResult Build(byte[] image, PeInfo pe, bool imageAligned, uint va, PeSection text)` — decodes forward via `Decoder.Create(32, ...)`, accumulates pattern+mask, applies wildcards per instruction, checks uniqueness every instruction past the minimum
  note: even on failure, hands back the grown (non-unique) pattern for the caller's ordinal-matching fallback — the only way to place a function the linker byte-folded with another
- L157 — `void ApplyWildcards(in Instruction instr, in ConstantOffsets co, List<bool> mask, int baseIdx, PeInfo pe)` — wildcards relative-branch displacements unconditionally; wildcards absolute 4-byte immediates/displacements only when `IsRelocationSensitive`
- L200 — `ulong GetImmediate(in Instruction instr)`
- L214 — `void Wildcard(List<bool> mask, int offset, int size)`
- L228 — `int CountMatches(byte[] image, PeInfo pe, bool imageAligned, PeSection text, byte[] pattern, bool[] mask, int limit, List<uint>? hitsOut)` — anchors on the first concrete byte for a fast single-pass scan of `.text`
