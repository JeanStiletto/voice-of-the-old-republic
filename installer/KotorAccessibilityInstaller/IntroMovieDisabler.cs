using System;
using System.Collections.Generic;
using System.IO;

namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// Rename the three launch-time intro <c>.bik</c> files
    /// (<c>biologo</c>, <c>leclogo</c>, <c>legal</c>) so the engine's
    /// <c>PlayMoviesAsync</c> queue fails to open them and falls through to
    /// the main menu. Cuts ~10–20 s off cold start and — more importantly —
    /// eliminates the "alt-tab during intro causes the engine to restart the
    /// entire intro sequence" failure mode that costs blind users 30–60 s of
    /// perceived-stuck time before the menu becomes responsive.
    ///
    /// In-game scripted cutscenes (Endar Spire opener, planet arrivals,
    /// endings, swoop transitions, ...) are unaffected — those go through a
    /// different engine path (<c>PlayMovie</c> / NWScript
    /// <c>ExecuteCommandPlayMovie</c>) and reference different file names.
    ///
    /// The community-standard mechanism is file renaming, not an ini flag
    /// — <c>[Game Options] Disable Movies=1</c> kills ALL movies including
    /// story cutscenes (verified via GOG/Steam community threads), and
    /// <c>[Movies Shown]</c> flags are an output (engine writes them after
    /// playback) rather than an input (engine reading them to decide
    /// whether to play).
    ///
    /// Idempotent: re-applying when files are already renamed is a no-op;
    /// restoring when files are already restored is a no-op. The runtime
    /// mod-settings toggle in <c>menus_modsettings.cpp</c> uses the same
    /// filesystem state as its persistence layer.
    /// </summary>
    public static class IntroMovieDisabler
    {
        private const string MoviesSubdir = "Movies";
        private const string DisabledSuffix = ".disabled";

        // The three known launch-time intro filenames. Verified against the
        // swkotor.exe string table (offset 0x0034ECF0): the three literals
        // sit together in the .data segment and are the only files referenced
        // by PlayMoviesAsync.
        private static readonly string[] IntroFiles =
        {
            "biologo.bik",
            "leclogo.bik",
            "legal.bik",
        };

        public sealed class Result
        {
            public bool Success { get; init; }
            public string Error { get; init; }
            public int Renamed { get; init; }
            public int AlreadyDone { get; init; }
            public int Missing { get; init; }
        }

        /// <summary>
        /// Rename each intro file to <c>&lt;name&gt;.bik.disabled</c>. Already
        /// renamed → counted as AlreadyDone. Original file missing entirely →
        /// counted as Missing (not an error; e.g. a previous user removal).
        /// </summary>
        public static Result DisableIntros(string gameDir)
        {
            string moviesDir = Path.Combine(gameDir, MoviesSubdir);
            if (!Directory.Exists(moviesDir))
            {
                return new Result
                {
                    Success = false,
                    Error = $"{MoviesSubdir} folder not found at {moviesDir}",
                };
            }

            int renamed = 0;
            int alreadyDone = 0;
            int missing = 0;
            var errors = new List<string>();

            foreach (var name in IntroFiles)
            {
                string src = Path.Combine(moviesDir, name);
                string dst = src + DisabledSuffix;

                if (File.Exists(dst))
                {
                    // Already disabled. If the original still exists alongside
                    // (mid-state from a previous failed run, or user restored
                    // manually), delete the original — the disabled one wins.
                    if (File.Exists(src))
                    {
                        try
                        {
                            File.Delete(src);
                            Logger.Info($"  Removed stray {name} (disabled copy already in place)");
                        }
                        catch (Exception ex)
                        {
                            errors.Add($"could not remove stray {name}: {ex.Message}");
                            continue;
                        }
                    }
                    alreadyDone++;
                    Logger.Info($"  {name} already disabled");
                    continue;
                }

                if (!File.Exists(src))
                {
                    missing++;
                    Logger.Info($"  {name} not found (skip — already removed or never present)");
                    continue;
                }

                try
                {
                    File.Move(src, dst);
                    renamed++;
                    Logger.Info($"  {name} -> {Path.GetFileName(dst)}");
                }
                catch (Exception ex)
                {
                    errors.Add($"could not rename {name}: {ex.Message}");
                }
            }

            if (errors.Count > 0)
            {
                return new Result
                {
                    Success = false,
                    Error = string.Join("; ", errors),
                    Renamed = renamed,
                    AlreadyDone = alreadyDone,
                    Missing = missing,
                };
            }

            Logger.Info($"  Intro disable: {renamed} renamed, {alreadyDone} already done, {missing} missing");

            return new Result
            {
                Success = true,
                Renamed = renamed,
                AlreadyDone = alreadyDone,
                Missing = missing,
            };
        }

        /// <summary>
        /// Reverse <see cref="DisableIntros"/>. Renames
        /// <c>&lt;name&gt;.bik.disabled</c> back to <c>&lt;name&gt;.bik</c>.
        /// Used by the uninstaller so removal returns the install to vanilla.
        /// Counts already-restored / both-present cases gracefully.
        /// </summary>
        public static Result RestoreIntros(string gameDir)
        {
            string moviesDir = Path.Combine(gameDir, MoviesSubdir);
            if (!Directory.Exists(moviesDir))
            {
                return new Result
                {
                    Success = false,
                    Error = $"{MoviesSubdir} folder not found at {moviesDir}",
                };
            }

            int renamed = 0;
            int alreadyDone = 0;
            int missing = 0;
            var errors = new List<string>();

            foreach (var name in IntroFiles)
            {
                string dst = Path.Combine(moviesDir, name);
                string src = dst + DisabledSuffix;

                if (File.Exists(dst))
                {
                    // Original already restored. If a stale .disabled also
                    // exists, drop it so we don't leave junk in the install.
                    if (File.Exists(src))
                    {
                        try
                        {
                            File.Delete(src);
                            Logger.Info($"  Removed stale {Path.GetFileName(src)} (original already restored)");
                        }
                        catch (Exception ex)
                        {
                            errors.Add($"could not remove stale {Path.GetFileName(src)}: {ex.Message}");
                            continue;
                        }
                    }
                    alreadyDone++;
                    Logger.Info($"  {name} already restored");
                    continue;
                }

                if (!File.Exists(src))
                {
                    missing++;
                    Logger.Info($"  {name} not present in either form (skip)");
                    continue;
                }

                try
                {
                    File.Move(src, dst);
                    renamed++;
                    Logger.Info($"  {Path.GetFileName(src)} -> {name}");
                }
                catch (Exception ex)
                {
                    errors.Add($"could not restore {name}: {ex.Message}");
                }
            }

            if (errors.Count > 0)
            {
                return new Result
                {
                    Success = false,
                    Error = string.Join("; ", errors),
                    Renamed = renamed,
                    AlreadyDone = alreadyDone,
                    Missing = missing,
                };
            }

            Logger.Info($"  Intro restore: {renamed} restored, {alreadyDone} already restored, {missing} missing");

            return new Result
            {
                Success = true,
                Renamed = renamed,
                AlreadyDone = alreadyDone,
                Missing = missing,
            };
        }
    }
}
