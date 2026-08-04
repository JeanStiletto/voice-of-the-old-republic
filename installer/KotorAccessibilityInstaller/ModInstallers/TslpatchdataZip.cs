using System;
using System.IO;
using System.IO.Compression;

namespace KotorAccessibilityInstaller.ModInstallers
{
    /// <summary>
    /// Pulls the <c>tslpatchdata/</c> folder out of a mod's distribution zip.
    ///
    /// <para>Every TSLPatcher-style archive has the same shape: the payload in
    /// <c>tslpatchdata/</c>, next to a bundled <c>INSTALL.exe</c> (TSLPatcher
    /// itself), a readme and sometimes a Source folder. We drive the payload
    /// with HoloPatcher and never run a bundled patcher exe, so everything
    /// outside <c>tslpatchdata/</c> is skipped.</para>
    ///
    /// <para>Entry paths are resolved against the destination and rejected if
    /// they escape it — a zip from a third-party site should not be trusted to
    /// stay inside its own folder.</para>
    /// </summary>
    internal static class TslpatchdataZip
    {
        /// <summary>Folder inside the archive holding the TSLPatcher payload.</summary>
        public const string DirName = "tslpatchdata";

        /// <summary>
        /// Extract <c>tslpatchdata/</c> from <paramref name="zipPath"/> into
        /// <paramref name="destRoot"/>, preserving the folder name (so the
        /// result is <c>&lt;destRoot&gt;/tslpatchdata/…</c>).
        /// </summary>
        /// <param name="logPrefix">Mod id used in the skipped-entry warning.</param>
        public static void Extract(string zipPath, string destRoot, string logPrefix)
        {
            string fullDest = Path.GetFullPath(destRoot);
            using var archive = ZipFile.OpenRead(zipPath);

            foreach (var entry in archive.Entries)
            {
                string normalized = entry.FullName.Replace('\\', '/');
                if (!normalized.StartsWith(DirName + "/", StringComparison.OrdinalIgnoreCase))
                    continue;

                string target = Path.GetFullPath(Path.Combine(destRoot, normalized.Replace('/', Path.DirectorySeparatorChar)));
                if (!target.StartsWith(fullDest + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
                {
                    Logger.Warning($"{logPrefix} archive entry escapes the staging dir; skipped: {entry.FullName}");
                    continue;
                }

                if (normalized.EndsWith("/", StringComparison.Ordinal))
                {
                    Directory.CreateDirectory(target);
                    continue;
                }

                Directory.CreateDirectory(Path.GetDirectoryName(target));
                entry.ExtractToFile(target, overwrite: true);
            }
        }
    }
}
