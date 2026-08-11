using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// In-place editor for the game's ini that applies the community-recommended
    /// stability tweaks alongside our screen-reader compatibility setting, and (on a
    /// full install only) the mod's recommended movement keybinds.
    ///
    /// Preserves all other sections, keys, ordering, comments, and trailing
    /// whitespace. Idempotent — re-running the installer doesn't double-up.
    ///
    /// Which keys land where is per-game and lives in <see cref="GameTarget.IniDefaults"/>;
    /// KOTOR 2 spells one of them differently and needs another in two sections.
    /// Stability rationale (sourced from the neocities full build's "Misc. Basegame
    /// Issues &amp; Fixes" section and our own accessibility-investigation.md):
    /// <list type="bullet">
    ///   <item><c>V-Sync=1</c> — fixes "character stuck after combat" engine bug on 60 Hz monitors</item>
    ///   <item><c>Frame Buffer=0</c> — fixes "crash after character creation" + occasional loadscreen crashes (KOTOR 1 only; see <see cref="GameTarget.Kotor1"/>)</item>
    ///   <item>vertex-buffer objects disabled — stability tweak for some GPU/driver combinations</item>
    ///   <item><c>FullScreen=0</c> — screen-reader compatibility (exclusive fullscreen breaks NVDA/JAWS)</item>
    ///   <item><c>AllowWindowedMode=1</c> — required by Lane's BorderlessFullscreen patch (KOTOR 2)</item>
    /// </list>
    /// </summary>
    public static class SwkotorIniTweaker
    {
        private const string KeymappingSectionHeader = "[Keymapping]";
        private const string SoundSectionHeader = "[Sound Options]";

        // Movement-keybind defaults for mod users. Values are the engine's
        // InputIndices (the decimal codes the ini stores), and they encode the
        // *physical* key position — so 76 is the bottom-left letter key (labelled Y
        // on a German keyboard, Z on a US keyboard) regardless of layout.
        //
        // We swap vanilla's two in-world action pairs so blind players get an
        // ergonomic bottom-row cluster: strafe on A/D, camera turn on Y/C. Confirmed
        // against keymap.2da:
        //   Action281 = ActionLeft/ActionRight   = on-foot strafe   (vanilla Z/C)
        //   Action284 = CameraRotateLeft/Right    = camera turn      (vanilla A/D)
        // Action283 (MGActionLeft/Right = minigame steering) is left on A/D so the
        // swoop/turret minigames are unaffected.
        //
        // Shared by both games: KOTOR 2's keymap.2da carries the same four rows
        // under the same ids with the same vanilla defaults (action281a/b = Z/C,
        // action284a/b = A/D, action283a/b = A/D), so one table serves both.
        private static readonly (string Key, string Value)[] KeymapTweaks =
        {
            ("Action281A", "51"),  // strafe left  = A
            ("Action281B", "54"),  // strafe right = D
            ("Action284A", "76"),  // camera turn left  = bottom-left key (Y on DE / Z on US)
            ("Action284B", "53"),  // camera turn right = C
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
        /// Apply <paramref name="target"/>'s stability + accessibility defaults to
        /// its ini. Missing keys are appended to their section; a missing section is
        /// appended at end of file. Entries are grouped by section and each section
        /// is edited in one pass, so a target that touches two sections still reads
        /// and writes the file once per section rather than per key.
        ///
        /// The per-section results are folded into one: success only if every
        /// section succeeded, with the counts summed and the first error kept.
        /// </summary>
        public static Result ApplyAccessibilityDefaults(GameTarget target, string gameDir)
        {
            var bySection = new Dictionary<string, List<(string Key, string Value)>>(StringComparer.OrdinalIgnoreCase);
            foreach (var (section, key, value) in target.IniDefaults)
            {
                if (!bySection.TryGetValue(section, out var list))
                    bySection[section] = list = new List<(string, string)>();
                list.Add((key, value));
            }

            Result folded = null;
            foreach (var kvp in bySection)
            {
                var r = ApplySectionPairs(gameDir, target.IniFileName, kvp.Key, kvp.Value.ToArray());
                folded = folded == null ? r : Combine(folded, r);
            }

            return folded ?? new Result { Success = true, IniPath = Path.Combine(gameDir, target.IniFileName) };
        }

        private static Result Combine(Result a, Result b) => new Result
        {
            Success = a.Success && b.Success,
            Error = a.Error ?? b.Error,
            Changed = a.Changed + b.Changed,
            AlreadyCorrect = a.AlreadyCorrect + b.AlreadyCorrect,
            Added = a.Added + b.Added,
            IniPath = a.IniPath,
        };

        /// <summary>
        /// Apply the mod's movement keybinds (strafe on A/D, camera turn on Y/C)
        /// plus the target's <see cref="GameTarget.KeymapExtras"/> (KOTOR 2:
        /// SwitchWeaps off H) to the <c>[Keymapping]</c> section of the target's
        /// ini. Full-install only — the caller must skip this on the update path
        /// so a returning player's customised bindings are never overwritten.
        /// </summary>
        public static Result ApplyKeymapDefaults(GameTarget target, string gameDir)
            => ApplySectionPairs(gameDir, target.IniFileName, KeymappingSectionHeader,
                                 KeymapTweaksFor(target));

        /// <summary>Shared movement pairs plus the target's per-game extras.</summary>
        private static (string Key, string Value)[] KeymapTweaksFor(GameTarget target)
        {
            var all = new List<(string Key, string Value)>(KeymapTweaks);
            if (target.KeymapExtras != null) all.AddRange(target.KeymapExtras);
            return all.ToArray();
        }

        /// <summary>
        /// Remove the mod's movement keybind lines from <c>[Keymapping]</c>, so
        /// the game falls back to its own <c>keymap.2da</c> defaults (strafe on
        /// Z/C, camera turn on A/D).
        ///
        /// <para>Uninstall path. Deleting the lines is right where writing the
        /// vanilla values would be wrong: an absent <c>Action281A</c> means "use
        /// the game's default", which is what an uninstalled mod should leave
        /// behind, and it stays correct even if the defaults differ from what we
        /// think they are.</para>
        ///
        /// <para>Only the four keys the mod writes are touched. A binding the
        /// player customised themselves is a different key or a different value,
        /// and either way is not ours to remove — the cost of that caution is
        /// that a player who happened to choose our exact layout by hand loses
        /// it, which is recoverable in the game's own options screen.</para>
        /// </summary>
        public static Result RemoveKeymapDefaults(GameTarget target, string gameDir)
        {
            var ourTweaks = KeymapTweaksFor(target);
            var ourKeys = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var (key, _) in ourTweaks) ourKeys.Add(key);

            return RemoveSectionKeys(gameDir, target.IniFileName, KeymappingSectionHeader, ourKeys,
                                     onlyWhenValueIs: ourTweaks);
        }

        /// <summary>
        /// Delete keys from a section, but only where the value is still the one
        /// we wrote — a line the player has since changed is theirs.
        /// </summary>
        private static Result RemoveSectionKeys(
            string gameDir, string iniFileName, string sectionHeader,
            HashSet<string> keys, (string Key, string Value)[] onlyWhenValueIs)
        {
            string iniPath = Path.Combine(gameDir, iniFileName);
            if (!File.Exists(iniPath))
            {
                // Nothing to undo. Not an error: the player may have deleted the
                // ini, or the game may never have been launched.
                return new Result { Success = true, IniPath = iniPath };
            }

            var expected = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            foreach (var (key, value) in onlyWhenValueIs) expected[key] = value;

            try
            {
                var lines = new List<string>(File.ReadAllLines(iniPath));
                int sectionStart = FindSectionStart(lines, sectionHeader);
                if (sectionStart < 0)
                    return new Result { Success = true, IniPath = iniPath };

                int sectionEndExclusive = FindNextSectionStart(lines, sectionStart + 1);
                int removed = 0;

                for (int i = sectionEndExclusive - 1; i > sectionStart; i--)
                {
                    string line = lines[i];
                    int eq = line.IndexOf('=');
                    if (eq <= 0) continue;

                    string key = line.Substring(0, eq).Trim();
                    if (!keys.Contains(key)) continue;

                    string value = line.Substring(eq + 1).Trim();
                    if (expected.TryGetValue(key, out string ours) &&
                        !value.Equals(ours, StringComparison.Ordinal))
                    {
                        Logger.Info($"  {key}={value} was changed by the player; left alone");
                        continue;
                    }

                    lines.RemoveAt(i);
                    removed++;
                    Logger.Info($"  {key} removed (game default restored)");
                }

                if (removed == 0)
                    return new Result { Success = true, IniPath = iniPath };

                var sb = new StringBuilder();
                foreach (var line in lines) sb.Append(line).Append("\r\n");
                File.WriteAllText(iniPath, sb.ToString(), new UTF8Encoding(false));

                Logger.Info($"  {iniFileName}: {removed} key(s) removed from {sectionHeader}");
                return new Result { Success = true, Changed = removed, IniPath = iniPath };
            }
            catch (Exception ex)
            {
                Logger.Error($"Failed to remove {sectionHeader} keys from {iniFileName}", ex);
                return new Result { Success = false, Error = ex.Message, IniPath = iniPath };
            }
        }

        /// <summary>
        /// Set <c>EAX</c> under <c>[Sound Options]</c> to 1 or 0. Drives the
        /// optional dsoal spatial-audio toggle, which needs EAX on to get the
        /// environmental reverb that is the whole point of the feature.
        ///
        /// Unlike the three calls above this is a toggle rather than a set of
        /// defaults, which is why it takes a parameter. SpatialAudioManager used
        /// to carry its own copy of the section-walk for this one key.
        /// </summary>
        /// <remarks>
        /// KOTOR 1 only: dsoal is not offered for KOTOR 2 (its audio stack is
        /// Aspyr's rebuild and the pairing is unverified), so this deliberately
        /// takes no target.
        /// </remarks>
        public static Result ApplyEaxSetting(string gameDir, bool enable)
            => ApplySectionPairs(gameDir, GameTarget.Kotor1.IniFileName, SoundSectionHeader,
                                 new[] { ("EAX", enable ? "1" : "0") });

        /// <summary>
        /// Shared in-place editor: sets each <paramref name="pairs"/> key=value inside
        /// <paramref name="sectionHeader"/> of <paramref name="iniFileName"/>, appending
        /// missing keys to the section and the section itself if absent. Preserves
        /// ordering, comments, and whitespace.
        /// </summary>
        private static Result ApplySectionPairs(
            string gameDir, string iniFileName, string sectionHeader, (string Key, string Value)[] pairs)
        {
            string iniPath = Path.Combine(gameDir, iniFileName);
            if (!File.Exists(iniPath))
            {
                return new Result
                {
                    Success = false,
                    Error = $"{iniFileName} not found at {iniPath}",
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
                foreach (var (key, value) in pairs) remaining[key] = value;

                int changed = 0;
                int alreadyCorrect = 0;
                int added = 0;

                int sectionStart = FindSectionStart(lines, sectionHeader);
                int sectionEndExclusive;

                if (sectionStart < 0)
                {
                    // Section missing entirely — append a fresh one at EOF and dump all tweaks into it.
                    if (lines.Count > 0 && !string.IsNullOrEmpty(lines[^1])) lines.Add(string.Empty);
                    lines.Add(sectionHeader);
                    sectionStart = lines.Count - 1;
                    sectionEndExclusive = lines.Count;
                    Logger.Info($"  {sectionHeader} not found in {iniFileName}; appending");
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

                Logger.Info($"  {iniFileName} updated ({changed} changed, {added} added, {alreadyCorrect} already correct)");

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
                Logger.Error($"Failed to apply {sectionHeader} settings to {iniFileName}", ex);
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
