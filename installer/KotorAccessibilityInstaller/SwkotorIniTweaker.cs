using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// In-place editor for <c>swkotor.ini</c> that applies the community-recommended
    /// stability tweaks alongside our screen-reader compatibility setting.
    ///
    /// Targets the <c>[Graphics Options]</c> section. Preserves all other sections,
    /// keys, ordering, comments, and trailing whitespace. Idempotent — re-running
    /// the installer doesn't double-up.
    ///
    /// Tweaks applied (sourced from the neocities full build's "Misc. Basegame
    /// Issues &amp; Fixes" section and our own accessibility-investigation.md):
    /// <list type="bullet">
    ///   <item><c>V-Sync=1</c> — fixes "character stuck after combat" engine bug on 60 Hz monitors</item>
    ///   <item><c>Frame Buffer=0</c> — fixes "crash after character creation" + occasional loadscreen crashes</item>
    ///   <item><c>Disable Vertex Buffer Objects=1</c> — stability tweak for some GPU/driver combinations</item>
    ///   <item><c>FullScreen=0</c> — screen-reader compatibility (exclusive fullscreen breaks NVDA/JAWS)</item>
    /// </list>
    /// </summary>
    public static class SwkotorIniTweaker
    {
        private const string IniFileName = "swkotor.ini";
        private const string GraphicsSectionHeader = "[Graphics Options]";

        // Exact key spellings the engine reads. Case-sensitive on the engine side.
        private static readonly (string Key, string Value)[] Tweaks =
        {
            ("V-Sync", "1"),
            ("Frame Buffer", "0"),
            ("Disable Vertex Buffer Objects", "1"),
            ("FullScreen", "0"),
        };

        public sealed class Result
        {
            public bool Success { get; init; }
            public string Error { get; init; }
            public int Changed { get; init; }
            public int AlreadyCorrect { get; init; }
            public int Added { get; init; }
            public string IniPath { get; init; }
        }

        /// <summary>
        /// Apply the stability tweaks to <c>&lt;gameDir&gt;/swkotor.ini</c>. Reads the
        /// file, modifies the relevant keys inside <c>[Graphics Options]</c>, writes
        /// it back. Missing keys are appended to the section. Missing section is
        /// appended at end of file.
        /// </summary>
        public static Result ApplyAccessibilityDefaults(string gameDir)
        {
            string iniPath = Path.Combine(gameDir, IniFileName);
            if (!File.Exists(iniPath))
            {
                return new Result
                {
                    Success = false,
                    Error = $"{IniFileName} not found at {iniPath}",
                    IniPath = iniPath
                };
            }

            try
            {
                // ReadAllLines strips the trailing newline; we restore it explicitly on write
                // so we don't drift from the engine's expected CRLF layout.
                var lines = new List<string>(File.ReadAllLines(iniPath));

                // Build a quick "which tweaks still need to land" set so we can detect
                // missing keys and append them at the end of the section.
                var remaining = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
                foreach (var (key, value) in Tweaks) remaining[key] = value;

                int changed = 0;
                int alreadyCorrect = 0;
                int added = 0;

                int sectionStart = FindSectionStart(lines, GraphicsSectionHeader);
                int sectionEndExclusive;

                if (sectionStart < 0)
                {
                    // Section missing entirely — append a fresh one at EOF and dump all tweaks into it.
                    if (lines.Count > 0 && !string.IsNullOrEmpty(lines[^1])) lines.Add(string.Empty);
                    lines.Add(GraphicsSectionHeader);
                    sectionStart = lines.Count - 1;
                    sectionEndExclusive = lines.Count;
                    Logger.Info($"  {GraphicsSectionHeader} not found in {IniFileName}; appending");
                }
                else
                {
                    sectionEndExclusive = FindNextSectionStart(lines, sectionStart + 1);
                }

                // Walk the section body, replacing any matching key=value pairs.
                for (int i = sectionStart + 1; i < sectionEndExclusive; i++)
                {
                    string line = lines[i];
                    int eq = line.IndexOf('=');
                    if (eq <= 0) continue;

                    string key = line.Substring(0, eq).Trim();
                    if (!remaining.TryGetValue(key, out string desired)) continue;

                    string currentValue = line.Substring(eq + 1).Trim();
                    if (currentValue.Equals(desired, StringComparison.Ordinal))
                    {
                        alreadyCorrect++;
                        Logger.Info($"  {key}={currentValue} already correct");
                    }
                    else
                    {
                        lines[i] = $"{key}={desired}";
                        changed++;
                        Logger.Info($"  {key}: {currentValue} -> {desired}");
                    }
                    remaining.Remove(key);
                }

                // Any keys we didn't find in the section get appended just before the
                // section ends (i.e. just before the next [section] header, or at EOF).
                if (remaining.Count > 0)
                {
                    int insertAt = sectionEndExclusive;
                    foreach (var kvp in remaining)
                    {
                        lines.Insert(insertAt++, $"{kvp.Key}={kvp.Value}");
                        added++;
                        Logger.Info($"  {kvp.Key}={kvp.Value} added (was missing)");
                    }
                }

                if (changed == 0 && added == 0)
                {
                    Logger.Info("  All stability tweaks already in place — no write needed");
                    return new Result
                    {
                        Success = true,
                        Changed = 0,
                        AlreadyCorrect = alreadyCorrect,
                        Added = 0,
                        IniPath = iniPath
                    };
                }

                // Engine reads CRLF; preserve that even on a Mono/Wine future.
                var sb = new StringBuilder();
                foreach (var line in lines) sb.Append(line).Append("\r\n");
                File.WriteAllText(iniPath, sb.ToString(), new UTF8Encoding(false));

                Logger.Info($"  swkotor.ini updated ({changed} changed, {added} added, {alreadyCorrect} already correct)");

                return new Result
                {
                    Success = true,
                    Changed = changed,
                    AlreadyCorrect = alreadyCorrect,
                    Added = added,
                    IniPath = iniPath
                };
            }
            catch (Exception ex)
            {
                Logger.Error("Failed to apply stability tweaks to swkotor.ini", ex);
                return new Result
                {
                    Success = false,
                    Error = ex.Message,
                    IniPath = iniPath
                };
            }
        }

        private static int FindSectionStart(List<string> lines, string sectionHeader)
        {
            for (int i = 0; i < lines.Count; i++)
            {
                if (lines[i].Trim().Equals(sectionHeader, StringComparison.OrdinalIgnoreCase))
                    return i;
            }
            return -1;
        }

        private static int FindNextSectionStart(List<string> lines, int from)
        {
            for (int i = from; i < lines.Count; i++)
            {
                string t = lines[i].Trim();
                if (t.StartsWith("[", StringComparison.Ordinal) && t.EndsWith("]", StringComparison.Ordinal))
                    return i;
            }
            return lines.Count;
        }
    }
}
