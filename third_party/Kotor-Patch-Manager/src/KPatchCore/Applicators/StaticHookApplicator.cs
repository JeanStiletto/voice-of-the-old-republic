using KPatchCore.Models;
using KPatchCore.Parsers;

namespace KPatchCore.Applicators;

/// <summary>
/// Applies STATIC hooks to executable files at install-time
/// </summary>
public static class StaticHookApplicator
{
    /// <summary>
    /// Applies static hooks to an executable file
    /// </summary>
    /// <param name="exePath">Path to executable to patch</param>
    /// <param name="hooks">Hooks to apply (will filter to only STATIC hooks)</param>
    /// <returns>Result indicating success or failure</returns>
    public static PatchResult ApplyStaticHooks(string exePath, List<Hook> hooks)
    {
        if (string.IsNullOrWhiteSpace(exePath))
        {
            return PatchResult.Fail("Executable path cannot be null or empty");
        }

        if (!File.Exists(exePath))
        {
            return PatchResult.Fail($"Executable not found: {exePath}");
        }

        // Filter to only STATIC hooks
        var staticHooks = hooks.Where(h => h.Type == HookType.Static).ToList();
        if (staticHooks.Count == 0)
        {
            return PatchResult.Ok("No static hooks to apply");
        }

        // Parse PE headers once for all hooks
        var peResult = PeHeaderParser.ParsePeHeaders(exePath);
        if (!peResult.Success || peResult.Data == null)
        {
            return PatchResult.Fail($"Failed to parse PE headers: {peResult.Error}");
        }

        var peInfo = peResult.Data;
        var errors = new List<string>();
        var appliedCount = 0;

        foreach (var hook in staticHooks)
        {
            // Convert virtual address to file offset
            var offsetResult = PeHeaderParser.VirtualAddressToFileOffset(peInfo, hook.Address);
            if (!offsetResult.Success)
            {
                errors.Add($"Hook at 0x{hook.Address:X8}: {offsetResult.Error}");
                continue;
            }

            var fileOffset = offsetResult.Data!;

            // Read current bytes at location
            var readResult = PeHeaderParser.ReadBytesAtVirtualAddress(
                exePath,
                peInfo,
                hook.Address,
                hook.OriginalBytes.Length);

            if (!readResult.Success || readResult.Data == null)
            {
                errors.Add($"Hook at 0x{hook.Address:X8}: Failed to read bytes: {readResult.Error}");
                continue;
            }

            // Verify original bytes match. If the replacement bytes are already present,
            // treat this hook as already applied. This keeps KPM-managed reapply flows from
            // failing solely because a STATIC patch previously changed the executable.
            var actualBytes = readResult.Data;
            if (!hook.OriginalBytes.SequenceEqual(actualBytes))
            {
                if (hook.ReplacementBytes != null && hook.ReplacementBytes.SequenceEqual(actualBytes))
                {
                    appliedCount++;
                    continue;
                }

                var expectedHex = BitConverter.ToString(hook.OriginalBytes).Replace("-", " ");
                var actualHex = BitConverter.ToString(actualBytes).Replace("-", " ");
                errors.Add($"Hook at 0x{hook.Address:X8}: Byte mismatch - expected [{expectedHex}], got [{actualHex}]");
                continue;
            }

            // Write replacement bytes
            var writeResult = PeHeaderParser.WriteBytesToVirtualAddress(
                exePath,
                peInfo,
                hook.Address,
                hook.ReplacementBytes!);

            if (!writeResult.Success)
            {
                errors.Add($"Hook at 0x{hook.Address:X8}: Failed to write bytes: {writeResult.Error}");
                continue;
            }

            appliedCount++;
        }

        // If any errors occurred, return failure
        if (errors.Count > 0)
        {
            return PatchResult.Fail(
                $"Failed to apply {errors.Count}/{staticHooks.Count} static hook(s):\n  - {string.Join("\n  - ", errors)}");
        }

        return PatchResult.Ok($"Successfully applied {appliedCount} static hook(s) to {Path.GetFileName(exePath)}");
    }

    /// <summary>
    /// Puts back what <see cref="ApplyStaticHooks"/> wrote: restores each static
    /// hook's original bytes wherever its replacement bytes are still in place.
    ///
    /// <para>Needed because a STATIC hook is the one thing an install leaves in
    /// the game's own executable, and <see cref="PatchRemover"/> can only undo it
    /// by restoring a backup. An install that ran with <c>CreateBackup = false</c>
    /// — reasonable when the game is a click away in a store client — therefore
    /// had no way back at all, and left an executable that no longer matches any
    /// known build behind for the next install to puzzle over.</para>
    ///
    /// <para>Byte-exact and conservative: a site already holding its original
    /// bytes is counted as done, and a site holding neither original nor
    /// replacement is left untouched and reported. Something else owns those
    /// bytes now — another mod, another patcher — and writing "originals" over
    /// them would corrupt its work. That case is reported rather than failed, so
    /// an uninstall still completes; only a genuine read/write or PE-parse
    /// failure fails the call.</para>
    /// </summary>
    /// <summary>
    /// What <see cref="RevertStaticHooks"/> did, carried on the result's
    /// <see cref="PatchResult.Data"/>. A caller that tries several candidate
    /// hooks files against one executable needs to tell "reverted nothing
    /// because this is the wrong build" from "reverted nothing because somebody
    /// else owns those bytes", and that is not a question to answer by reading
    /// the summary string.
    /// </summary>
    public sealed class RevertSummary
    {
        /// <summary>Sites put back to their original bytes.</summary>
        public int Reverted { get; init; }

        /// <summary>Sites that already held their original bytes.</summary>
        public int AlreadyOriginal { get; init; }

        /// <summary>Sites holding neither original nor replacement, left untouched.</summary>
        public List<string> NotOurs { get; init; } = new();
    }

    /// <param name="exePath">Path to the executable to revert</param>
    /// <param name="hooks">Hooks to revert (filtered to STATIC, as above)</param>
    public static PatchResult RevertStaticHooks(string exePath, List<Hook> hooks)
    {
        if (string.IsNullOrWhiteSpace(exePath))
        {
            return PatchResult.Fail("Executable path cannot be null or empty");
        }

        if (!File.Exists(exePath))
        {
            return PatchResult.Fail($"Executable not found: {exePath}");
        }

        var staticHooks = hooks.Where(h => h.Type == HookType.Static).ToList();
        if (staticHooks.Count == 0)
        {
            return PatchResult.Ok("No static hooks to revert");
        }

        var peResult = PeHeaderParser.ParsePeHeaders(exePath);
        if (!peResult.Success || peResult.Data == null)
        {
            return PatchResult.Fail($"Failed to parse PE headers: {peResult.Error}");
        }

        var peInfo = peResult.Data;
        var errors = new List<string>();
        var foreign = new List<string>();
        var revertedCount = 0;
        var alreadyOriginalCount = 0;

        foreach (var hook in staticHooks)
        {
            if (hook.ReplacementBytes == null)
            {
                continue;
            }

            var readResult = PeHeaderParser.ReadBytesAtVirtualAddress(
                exePath,
                peInfo,
                hook.Address,
                hook.ReplacementBytes.Length);

            if (!readResult.Success || readResult.Data == null)
            {
                errors.Add($"Hook at 0x{hook.Address:X8}: Failed to read bytes: {readResult.Error}");
                continue;
            }

            var actualBytes = readResult.Data;

            if (hook.OriginalBytes.SequenceEqual(actualBytes))
            {
                alreadyOriginalCount++;
                continue;
            }

            if (!hook.ReplacementBytes.SequenceEqual(actualBytes))
            {
                var actualHex = BitConverter.ToString(actualBytes).Replace("-", " ");
                foreign.Add($"0x{hook.Address:X8} holds [{actualHex}]");
                continue;
            }

            var writeResult = PeHeaderParser.WriteBytesToVirtualAddress(
                exePath,
                peInfo,
                hook.Address,
                hook.OriginalBytes);

            if (!writeResult.Success)
            {
                errors.Add($"Hook at 0x{hook.Address:X8}: Failed to write bytes: {writeResult.Error}");
                continue;
            }

            revertedCount++;
        }

        if (errors.Count > 0)
        {
            return PatchResult.Fail(
                $"Failed to revert {errors.Count}/{staticHooks.Count} static hook(s):\n  - {string.Join("\n  - ", errors)}");
        }

        var summary = $"Reverted {revertedCount} static hook(s) in {Path.GetFileName(exePath)}" +
                      (alreadyOriginalCount > 0 ? $"; {alreadyOriginalCount} already original" : "") +
                      (foreign.Count > 0
                          ? $"; left {foreign.Count} site(s) alone, not ours anymore: {string.Join(", ", foreign)}"
                          : "");

        return PatchResult.Ok(summary, new RevertSummary
        {
            Reverted = revertedCount,
            AlreadyOriginal = alreadyOriginalCount,
            NotOurs = foreign
        });
    }
}
