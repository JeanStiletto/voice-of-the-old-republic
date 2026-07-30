using Iced.Intel;

namespace Kdev;

/// <summary>
/// A byte pattern with per-byte wildcards, anchored at a known address.
/// </summary>
/// <param name="Pattern">Expected bytes; positions where Mask is false are ignored.</param>
/// <param name="Mask">true = this byte must match exactly.</param>
public sealed record Signature(byte[] Pattern, bool[] Mask, int InstructionCount)
{
    public int Length => Pattern.Length;

    /// <summary>Concrete (non-wildcard) byte count — the real discriminating power.</summary>
    public int ConcreteBytes => Mask.Count(m => m);

    /// <summary>IDA-style "48 8B ?? ?? E8" rendering, for reports and hooks.toml comments.</summary>
    public override string ToString() =>
        string.Join(' ', Pattern.Select((b, i) => Mask[i] ? b.ToString("x2") : "??"));
}

/// <summary>
/// Builds and resolves relocation-tolerant code signatures.
///
/// The problem
/// -----------
/// Two builds of the same engine contain the same functions with the same
/// codegen, but the linker placed them at slightly different addresses (we
/// measured +96..+512 bytes, varying per function and NOT monotonic). So a
/// function's bytes are identical *except* for any operand that encodes an
/// address which moved:
///
///   * `call rel32` / `jmp rel32` / `jcc rel8|rel32` — the displacement is
///     target-minus-next-instruction, so it changes whenever caller and callee
///     moved by different amounts. Nearly every function contains one, which
///     is why naive byte matching fails past the first few instructions.
///   * `push offset someFunction` and similar absolute code pointers.
///
/// What must NOT be wildcarded is just as important: absolute displacements
/// into .data are *unchanged* between these builds (verified — .data has an
/// identical VirtualSize in both, and our mss32 IAT constant resolves to the
/// identical slot). Those are the most discriminating bytes available, so they
/// stay concrete.
///
/// .rdata is the subtle one. It looks like read-only data that ought to be
/// stable, but its VirtualSize differs between the two builds (0x4FBFE vs
/// 0x4FC1E), so everything past the edit shifted — and .rdata is where vtable
/// pointers and string literals live. Constructors are full of
/// `mov [esi], offset vtable`, which is exactly why the first pass resolved
/// 205 of 216 addresses but failed on four constructors/destructors. So
/// .rdata references are treated as relocation-sensitive too.
/// </summary>
public static class Signatures
{
    /// <summary>
    /// Give up rather than emit an enormous pattern; also bounds the scan cost.
    /// 96 was too tight: several small functions are byte-identical to another
    /// function for their first 96 bytes and only diverge later.
    /// </summary>
    public const int MaxBytes = 256;

    /// <summary>Start here so short signatures do not collide by luck (a 5-byte
    /// pattern matched 2,517 places in this .text when we measured it).</summary>
    public const int MinInstructions = 4;

    public sealed record BuildResult(Signature? Signature, string? Failure, int MatchesAtFinalLength);

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
    {
        if (addr < pe.ImageBase) return false;
        var sec = pe.SectionForRva(addr - pe.ImageBase);
        return sec != null && (sec.Name == ".text" || sec.Name == ".rdata");
    }

    public static BuildResult Build(
        byte[] image, PeInfo pe, bool imageAligned, uint va, PeSection text)
    {
        int start = pe.VaToOffset(va, imageAligned);
        if (start < 0) return new BuildResult(null, "address is not inside any section", 0);

        // Decode forward, accumulating bytes + mask one instruction at a time.
        var reader = new ByteArrayCodeReader(image, start, Math.Min(MaxBytes + 16, image.Length - start));
        var decoder = Decoder.Create(32, reader, va);

        var pattern = new List<byte>(MaxBytes);
        var mask = new List<bool>(MaxBytes);
        int instrCount = 0;
        int lastMatches = int.MaxValue;

        while (pattern.Count < MaxBytes)
        {
            ulong ip = decoder.IP;
            decoder.Decode(out Instruction instr);
            if (instr.IsInvalid)
                return new BuildResult(null, $"undecodable instruction at 0x{ip:X8}", 0);

            int len = instr.Length;
            int off = start + (int)(ip - va);
            if (off + len > image.Length)
                return new BuildResult(null, "ran off the end of the image", 0);

            var co = decoder.GetConstantOffsets(instr);

            for (int i = 0; i < len; i++)
            {
                pattern.Add(image[off + i]);
                mask.Add(true);
            }

            int baseIdx = pattern.Count - len;
            ApplyWildcards(instr, co, mask, baseIdx, pe);
            instrCount++;

            if (instrCount < MinInstructions) continue;

            lastMatches = CountMatches(image, pe, imageAligned, text,
                                       pattern.ToArray(), mask.ToArray(), limit: 2);
            if (lastMatches == 1)
                return new BuildResult(
                    new Signature(pattern.ToArray(), mask.ToArray(), instrCount), null, 1);
            if (lastMatches == 0)
                // Cannot happen — the origin always matches — but a bug in the
                // wildcarding would show up exactly here, so say so loudly.
                return new BuildResult(null, "internal error: signature does not match its own origin", 0);
        }

        // Report the shape, not just the failure: a pattern that stayed
        // ambiguous over many *short* instructions is usually decoded garbage
        // (a wrong start address lands mid-instruction and yields runs of
        // 1-byte opcodes), whereas few long instructions with little concrete
        // content is a genuinely repetitive stub.
        int concrete = mask.Count(m => m);
        // Hand back the grown pattern even though it is not unique: the caller
        // can still use it for ordinal matching (find every identical site in
        // both binaries and pair them by address order), which is the only way
        // to place a function whose code the linker folded with another's.
        return new BuildResult(
            new Signature(pattern.ToArray(), mask.ToArray(), instrCount),
            $"not unique within {MaxBytes} bytes ({instrCount} instructions, " +
            $"{concrete} concrete bytes, still {lastMatches}+ matches)", lastMatches);
    }

    /// <summary>
    /// Mark the operand bytes that legitimately differ between builds.
    /// Everything else stays concrete.
    /// </summary>
    private static void ApplyWildcards(
        in Instruction instr, in ConstantOffsets co,
        List<bool> mask, int baseIdx, PeInfo pe)
    {
        // Relative branches: the displacement is relative to the *next*
        // instruction, so it shifts whenever caller and callee moved apart.
        bool isRelBranch = false;
        for (int op = 0; op < instr.OpCount; op++)
        {
            var kind = instr.GetOpKind(op);
            if (kind is OpKind.NearBranch16 or OpKind.NearBranch32 or OpKind.NearBranch64)
            {
                isRelBranch = true;
                break;
            }
        }

        if (isRelBranch && co.HasImmediate)
        {
            Wildcard(mask, baseIdx + co.ImmediateOffset, co.ImmediateSize);
            return;
        }

        // Absolute pointers baked in as immediates: `push offset fn`,
        // `mov reg, offset fn`, and crucially `mov [esi], offset vtable` in
        // every constructor.
        if (co.HasImmediate && co.ImmediateSize == 4)
        {
            if (IsRelocationSensitive(pe, (uint)GetImmediate(instr)))
                Wildcard(mask, baseIdx + co.ImmediateOffset, co.ImmediateSize);
        }

        // Absolute memory displacements — jump tables, function-pointer tables,
        // reads of .rdata constants. Displacements into .data stay concrete:
        // that section is byte-stable across these builds and is the strongest
        // discriminator available.
        if (co.HasDisplacement && co.DisplacementSize == 4)
        {
            if (IsRelocationSensitive(pe, (uint)instr.MemoryDisplacement32))
                Wildcard(mask, baseIdx + co.DisplacementOffset, co.DisplacementSize);
        }
    }

    private static ulong GetImmediate(in Instruction instr)
    {
        for (int op = 0; op < instr.OpCount; op++)
        {
            switch (instr.GetOpKind(op))
            {
                case OpKind.Immediate32: return instr.Immediate32;
                case OpKind.Immediate32to64: return (ulong)instr.Immediate32to64;
                case OpKind.Immediate8to32: return (ulong)(uint)instr.Immediate8to32;
            }
        }
        return 0;
    }

    private static void Wildcard(List<bool> mask, int offset, int size)
    {
        for (int i = 0; i < size; i++)
        {
            int idx = offset + i;
            if (idx >= 0 && idx < mask.Count) mask[idx] = false;
        }
    }

    /// <summary>
    /// Count matches of a masked pattern inside .text, stopping at
    /// <paramref name="limit"/>. Uses the first concrete byte as an anchor so
    /// the common case is a single pass of byte compares.
    /// </summary>
    public static int CountMatches(
        byte[] image, PeInfo pe, bool imageAligned, PeSection text,
        byte[] pattern, bool[] mask, int limit = int.MaxValue,
        List<uint>? hitsOut = null)
    {
        int secStart = imageAligned ? (int)text.VirtualAddress : (int)text.RawPointer;
        int secLen = (int)(imageAligned
            ? Math.Max(text.VirtualSize, text.RawSize)
            : text.RawSize);
        int secEnd = Math.Min(secStart + secLen, image.Length) - pattern.Length;

        int anchor = Array.IndexOf(mask, true);
        if (anchor < 0) return int.MaxValue;  // all-wildcard: matches everywhere
        byte anchorByte = pattern[anchor];

        int count = 0;
        for (int i = secStart; i <= secEnd; i++)
        {
            if (image[i + anchor] != anchorByte) continue;

            bool ok = true;
            for (int j = 0; j < pattern.Length; j++)
            {
                if (!mask[j]) continue;
                if (image[i + j] != pattern[j]) { ok = false; break; }
            }
            if (!ok) continue;

            count++;
            hitsOut?.Add(pe.OffsetToVa(i, imageAligned));
            if (count >= limit) return count;
        }
        return count;
    }
}
