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
            => MoveIntros(gameDir, disable: true);

        /// <summary>
        /// Reverse <see cref="DisableIntros"/>. Renames
        /// <c>&lt;name&gt;.bik.disabled</c> back to <c>&lt;name&gt;.bik</c>.
        /// Used by the uninstaller so removal returns the install to vanilla.
        /// Counts already-restored / both-present cases gracefully.
        /// </summary>
        public static Result RestoreIntros(string gameDir)
            => MoveIntros(gameDir, disable: false);

        /// <summary>
        /// The rename, in whichever direction. Disable moves
        /// <c>x.bik → x.bik.disabled</c>; restore moves it back. The two used
        /// to be hand-mirrored 85-line bodies (Phase-3 B6) — identical in
        /// structure and differing only in which name is the source, which is
        /// exactly the kind of pair where a fix lands on one side only.
        ///
        /// Every branch is idempotent, because this runs on a real game folder
        /// that may be in any state — including half-converted by an earlier
        /// failed run:
        ///   target exists + source exists → delete the source, the target wins
        ///   target exists, no source      → AlreadyDone
        ///   neither exists                → Missing (NOT an error: the user may
        ///                                   have deleted the .bik themselves)
        ///   source only                   → the actual rename
        /// A per-file failure is collected and reported without aborting the
        /// other two, so one locked file cannot leave the set half-renamed.
        /// </summary>
        private static Result MoveIntros(string gameDir, bool disable)
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

            // Log verbs, so the two directions still read naturally in the log.
            string doneWord   = disable ? "disabled" : "restored";
            string loserWord  = disable ? "stray" : "stale";
            string summaryTag = disable ? "Intro disable" : "Intro restore";
            string summaryVerb = disable ? "renamed" : "restored";

            int renamed = 0;
            int alreadyDone = 0;
            int missing = 0;
            var errors = new List<string>();

            foreach (var name in IntroFiles)
            {
                string plain    = Path.Combine(moviesDir, name);
                string disabled = plain + DisabledSuffix;
                string src = disable ? plain : disabled;
                string dst = disable ? disabled : plain;

                if (File.Exists(dst))
                {
                    // Target already in place. If the source also exists — a
                    // mid-state from a failed run, or a manual user edit — drop
                    // it so the install isn't left holding both.
                    if (File.Exists(src))
                    {
                        try
                        {
                            File.Delete(src);
                            Logger.Info($"  Removed {loserWord} {Path.GetFileName(src)} " +
                                        $"({Path.GetFileName(dst)} already in place)");
                        }
                        catch (Exception ex)
                        {
                            errors.Add($"could not remove {loserWord} {Path.GetFileName(src)}: {ex.Message}");
                            continue;
                        }
                    }
                    alreadyDone++;
                    Logger.Info($"  {name} already {doneWord}");
                    continue;
                }

                if (!File.Exists(src))
                {
                    missing++;
                    Logger.Info($"  {name} not found in either form (skip — already removed or never present)");
                    continue;
                }

                try
                {
                    File.Move(src, dst);
                    renamed++;
                    Logger.Info($"  {Path.GetFileName(src)} -> {Path.GetFileName(dst)}");
                }
                catch (Exception ex)
                {
                    errors.Add($"could not rename {Path.GetFileName(src)}: {ex.Message}");
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

            Logger.Info($"  {summaryTag}: {renamed} {summaryVerb}, {alreadyDone} already {doneWord}, {missing} missing");

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
