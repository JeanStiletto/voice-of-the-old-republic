using System;
using System.Collections.Generic;

namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// Everything that differs between the two games, in one place.
    ///
    /// <para>Before this existed, each install step carried its own hard-coded
    /// <c>"swkotor.exe"</c> / <c>"swkotor.ini"</c> / intro-file list, so adding
    /// KOTOR 2 meant either a second copy of every step or a boolean threaded
    /// through all of them. A target record keeps one code path per step and
    /// puts the per-game facts where they can be read side by side — which is
    /// also the only way the surprises stay visible (K2 spells one ini key
    /// without spaces, and carries <c>FullScreen</c> in two sections).</para>
    ///
    /// <para>Deliberately NOT here: anything the runtime patch decides for
    /// itself. The DLL identifies its host from the executable's SHA-256 via
    /// KPatchCore, so the installer never has to tell it which game it is.</para>
    /// </summary>
    public sealed class GameTarget
    {
        /// <summary>Stable short id used in logs, CLI args and registry key names.</summary>
        public string Id { get; private init; }

        /// <summary>Executable filename — also how a valid install folder is recognised.</summary>
        public string ExeName { get; private init; }

        /// <summary>
        /// Process name as <see cref="System.Diagnostics.Process.GetProcessesByName"/>
        /// reports it (no extension). NOT derivable by trimming ".exe" in a way
        /// worth relying on: "swkotor" does not match a running "swkotor2", which
        /// is exactly the bug that let an install run against a live KOTOR 2.
        /// </summary>
        public string ProcessName { get; private init; }

        /// <summary>Settings file in the install root.</summary>
        public string IniFileName { get; private init; }

        /// <summary>Steam application id, used for the post-install launch URL.</summary>
        public string SteamAppId { get; private init; }

        /// <summary>Human-facing name for dialogs and summaries.</summary>
        public string DisplayName { get; private init; }

        /// <summary>
        /// Add/Remove Programs key name. Each game gets its own entry: they are
        /// separate installs in separate folders that can be added and removed
        /// independently, and a single shared entry could only ever record one
        /// InstallLocation.
        /// </summary>
        public string RegistryKeyName { get; private init; }

        /// <summary>
        /// Launch-time logo movies renamed aside at install time. See
        /// <see cref="IntroMovieDisabler"/> for the mechanism and
        /// <see cref="Kotor2"/> below for how the KOTOR 2 list was derived.
        /// </summary>
        public IReadOnlyList<string> IntroMovieFiles { get; private init; }

        /// <summary>
        /// Ini settings written on install, as (section, key, value). Sections
        /// are per-entry rather than per-target because KOTOR 2 needs the same
        /// key in two of them.
        /// </summary>
        public IReadOnlyList<(string Section, string Key, string Value)> IniDefaults { get; private init; }

        /// <summary>
        /// Third-party <c>.kpatch</c> files bundled with the installer and offered
        /// alongside our own, as (embedded-resource name, patch id). Each is
        /// included only when its manifest declares this exact executable's
        /// SHA-256 — see <see cref="InstallationManager.ApplyKPatch"/>.
        ///
        /// <para>They install in the SAME transaction as the accessibility patch,
        /// which is not incidental: these rewrite the executable, so applying
        /// them first in a transaction of their own would change the hash that
        /// the second transaction's version gate reads.</para>
        /// </summary>
        public IReadOnlyList<(string AssetName, string PatchId)> BundledPatches { get; private init; }

        /// <summary>
        /// Embedded-resource name of the vanilla <c>prioritygroups.2da</c> used
        /// as the base when the install has no Override copy yet. The two games
        /// ship different column layouts, so they cannot share one file.
        /// </summary>
        public string VanillaPriorityGroupResource { get; private init; }

        public override string ToString() => DisplayName;

        // ------------------------------------------------------------------

        /// <summary>
        /// Frame Buffer note (applies to KOTOR 1 only, and is the reason the
        /// two lists differ): the engine builds the save-slot thumbnail out of
        /// the framebuffer, so <c>Frame Buffer=0</c> makes the capture
        /// zero-sized and the engine's image-scaler divides by its area. We
        /// still set 0 on KOTOR 1 — it is the documented fix for the
        /// crash-after-character-creation and loadscreen faults on older
        /// drivers — and neutralise the side effect in <c>save_crash_guard.cpp</c>,
        /// which detours the scaler and returns a blank thumbnail instead of
        /// faulting. That guard is a KOTOR 1 hook whose address resolves to
        /// zero on KOTOR 2, so KOTOR 2 keeps its shipped <c>Frame Buffer=1</c>
        /// rather than being handed the divide-by-zero with no net under it.
        /// </summary>
        public static readonly GameTarget Kotor1 = new GameTarget
        {
            Id = "k1",
            ExeName = "swkotor.exe",
            ProcessName = "swkotor",
            IniFileName = "swkotor.ini",
            SteamAppId = "32370",
            DisplayName = "KOTOR 1",
            RegistryKeyName = "KotorAccessibility",
            VanillaPriorityGroupResource = "prioritygroups.2da",

            BundledPatches = new[]
            {
                (Config.WidescreenKPatchAssetName, Config.WidescreenPatchId),
            },

            // Verified against the swkotor.exe string table: these three sit
            // together in .data and are the only files PlayMoviesAsync opens.
            IntroMovieFiles = new[] { "biologo.bik", "leclogo.bik", "legal.bik" },

            IniDefaults = new[]
            {
                ("[Graphics Options]", "V-Sync", "1"),
                ("[Graphics Options]", "Frame Buffer", "0"),
                ("[Graphics Options]", "Disable Vertex Buffer Objects", "1"),
                ("[Graphics Options]", "FullScreen", "0"),
            },
        };

        /// <summary>
        /// KOTOR 2, Aspyr's 2015 rebuild (Steam and GOG share the build).
        ///
        /// <para>Ini deltas from KOTOR 1, all confirmed against a shipped
        /// swkotor2.ini: the vertex-buffer key is spelled without spaces;
        /// <c>FullScreen</c> exists in BOTH <c>[Display Options]</c> and
        /// <c>[Graphics Options]</c>, so windowed mode has to be set in both or
        /// the surviving one wins; <c>AllowWindowedMode=1</c> is absent from the
        /// shipped file and is required by Lane's BorderlessFullscreen patch.
        /// <c>Frame Buffer</c> is deliberately absent from this list — see
        /// <see cref="Kotor1"/>.</para>
        ///
        /// <para>Intro list: KOTOR 2 has no <c>biologo.bik</c>. Its readable
        /// string data references the stems <c>leclogo</c> and <c>legal</c>,
        /// same as KOTOR 1. <c>ObsidianEnt.bik</c> and <c>Aspyr.bik</c> exist in
        /// Movies/ and are launch-time logos by every other sign, but neither
        /// stem could be confirmed in the executable — Steam's wrapper encrypts
        /// the code section on disk, so absence there is not evidence. They are
        /// included because the mechanism fails safe (a movie the engine cannot
        /// open is skipped) and is reversed by both the uninstaller and the
        /// in-game toggle; if a logo is somehow load-bearing it shows up on the
        /// very first launch.</para>
        /// </summary>
        public static readonly GameTarget Kotor2 = new GameTarget
        {
            Id = "k2",
            ExeName = "swkotor2.exe",
            ProcessName = "swkotor2",
            IniFileName = "swkotor2.ini",
            SteamAppId = "208580",
            DisplayName = "KOTOR 2",
            RegistryKeyName = "KotorAccessibility_K2",
            VanillaPriorityGroupResource = "prioritygroups_k2.2da",

            // Lane's two static engine patches. Feature-equivalent to the
            // community 3C-FD tool's 4 GB + borderless entries, minus a second
            // exe-hex-patching GUI fighting our hash management.
            BundledPatches = new[]
            {
                (Config.K2FourGbKPatchAssetName, Config.K2FourGbPatchId),
                (Config.K2BorderlessKPatchAssetName, Config.K2BorderlessPatchId),
            },

            IntroMovieFiles = new[] { "leclogo.bik", "legal.bik", "ObsidianEnt.bik", "Aspyr.bik" },

            IniDefaults = new[]
            {
                ("[Graphics Options]", "V-Sync", "1"),
                ("[Graphics Options]", "DisableVertexBufferObjects", "1"),
                ("[Graphics Options]", "FullScreen", "0"),
                ("[Graphics Options]", "AllowWindowedMode", "1"),
                ("[Display Options]", "FullScreen", "0"),
            },
        };

        /// <summary>Both targets, KOTOR 1 first. Order is load-bearing for the install flow.</summary>
        public static readonly IReadOnlyList<GameTarget> All = new[] { Kotor1, Kotor2 };

        /// <summary>Look a target up by <see cref="Id"/>; null when unknown.</summary>
        public static GameTarget FromId(string id)
        {
            foreach (var t in All)
                if (string.Equals(t.Id, id, StringComparison.OrdinalIgnoreCase))
                    return t;
            return null;
        }
    }
}
